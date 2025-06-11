#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "game.h"

#define XO_STATUS_FILE "/sys/module/kxo/initstate"
#define XO_DEVICE_FILE "/dev/kxo"
#define XO_DEVICE_ATTR_FILE "/sys/class/kxo/kxo/kxo_state"

// #ifdef DEBUG
// #define printde(...) printf(__VA_ARGS__)
// #else
// #define printde(...)
// #endif


static bool status_check(void)
{
    FILE *fp = fopen(XO_STATUS_FILE, "r");
    if (!fp) {
        printf("kxo status : not loaded\n");
        return false;
    }

    char read_buf[20];
    fgets(read_buf, 20, fp);
    read_buf[strcspn(read_buf, "\n")] = 0;
    if (strcmp("live", read_buf)) {
        printf("kxo status : %s\n", read_buf);
        fclose(fp);
        return false;
    }
    fclose(fp);
    return true;
}

static struct termios orig_termios;

static void raw_mode_disable(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void raw_mode_enable(void)
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(raw_mode_disable);
    struct termios raw = orig_termios;
    raw.c_iflag &= ~IXON;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static bool read_attr, end_attr;

static void listen_keyboard_handler(void)
{
    int attr_fd = open(XO_DEVICE_ATTR_FILE, O_RDWR);
    char input;

    if (read(STDIN_FILENO, &input, 1) == 1) {
        char buf[20];
        switch (input) {
        case 16: /* Ctrl-P */
            read(attr_fd, buf, 6);
            buf[0] = (buf[0] - '0') ? '0' : '1';
            read_attr ^= 1;
            write(attr_fd, buf, 6);
            if (!read_attr)
                printf("\n\nStopping to display the chess board...\n");
            break;
        case 17: /* Ctrl-Q */
            read(attr_fd, buf, 6);
            buf[4] = '1';
            read_attr = false;
            end_attr = true;
            write(attr_fd, buf, 6);
            printf("\n\nStopping the kernel space tic-tac-toe game...\n");
            break;
        }
    }
    close(attr_fd);
}

static void print_board(const char *arr, const char *mov, int len)
{
    int i;

    for (i = 1; i < 256; i <<= 1) {
        if (arr[0] & i) {
            printf("%c", (arr[2] & i) ? 'X' : 'O');
        } else {
            printf(" ");
        }
        if (i & 0x88) {
            printf("\n-------\n");
        } else {
            printf("|");
        }
    }
    for (i = 1; i < 256; i <<= 1) {
        if (arr[1] & i) {
            printf("%c", (arr[3] & i) ? 'X' : 'O');
        } else {
            printf(" ");
        }
        if (i & 8) {
            printf("\n-------\n");
        } else if (i & 0x80) {
            printf("\n");
        } else {
            printf("|");
        }
    }
    printf("\n\nmovement: %d\n", len);

    for (i = 0; i < len; i++) {
        printf("%c%d", (mov[i] >> 2) + 'A', mov[i] & 3);
        if (i != len - 1)
            printf(" -> ");
    }
    printf("\n");
}

static void print_win()
{
    int attr_fd = open(XO_DEVICE_ATTR_FILE, O_RDWR);
    char buf[20];
    read(attr_fd, buf, 8);
    if (buf[6] != ' ')
        printf("%c: win!!\n\n", buf[6]);
    close(attr_fd);
}


int main(int argc, char *argv[])
{
    if (!status_check())
        exit(1);

    raw_mode_enable();
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    fd_set readset;
    int device_fd = open(XO_DEVICE_FILE, O_RDONLY);
    int max_fd = device_fd > STDIN_FILENO ? device_fd : STDIN_FILENO;
    char board[4];
    char movement[16];
    read_attr = true;
    end_attr = false;
    // printf("%c\n", end_attr?'T':'F');

    while (!end_attr) {
        FD_ZERO(&readset);
        FD_SET(STDIN_FILENO, &readset);
        FD_SET(device_fd, &readset);

        int result = select(max_fd + 1, &readset, NULL, NULL, NULL);
        if (result < 0) {
            printf("Error with select system call\n");
            exit(1);
        }

        if (FD_ISSET(STDIN_FILENO, &readset)) {
            FD_CLR(STDIN_FILENO, &readset);
            listen_keyboard_handler();
        } else if (read_attr && FD_ISSET(device_fd, &readset)) {
            FD_CLR(device_fd, &readset);
            printf("\033[H\033[J"); /* ASCII escape code to clear the screen */
            read(device_fd, board, 4);
            char b_size;
            read(device_fd, &b_size, 1);
            b_size -= 5;
            if (b_size > 0) {
                read(device_fd, movement, (size_t) b_size);
            }
            print_board(board, movement, (int) b_size);
            print_win();
        }
    }

    raw_mode_disable();
    fcntl(STDIN_FILENO, F_SETFL, flags);

    close(device_fd);

    return 0;
}
