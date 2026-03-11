#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dswifi9.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>

#define SERVER_PORT 5050
#define SERVER_IP "37.143.163.56"

// Global buffers for credentials
char chat_user[32] = {0};
char chat_pass[32] = {0};

// Function to handle keyboard input
void get_user_input(const char* prompt, char* buffer, int max_len) {
    Keyboard* kb = keyboardDemoInit();
    keyboardShow();
    
    printf("\n%s\n> ", prompt);
    
    int index = 0;
    while(1) {
        swiWaitForVBlank();
        int c = keyboardUpdate();
        
        if (c == '\n') { // Enter key
            buffer[index] = '\0';
            break;
        } else if (c == '\b' && index > 0) { // Backspace
            index--;
            printf("\b \b"); // Move back, print space, move back again
        } else if (c > 0 && index < max_len - 1) {
            buffer[index++] = (char)c;
            printf("%c", (char)c);
        }
    }
    keyboardHide();
    printf("\n");
}

int main(void) {
    // 1. Setup Screens
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);

    PrintConsole topScreen, bottomScreen;
    consoleInit(&topScreen, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
    consoleInit(&bottomScreen, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);

    // 2. Get Credentials on Bottom Screen
    consoleSelect(&bottomScreen);
    printf("--- Login Setup ---\n");
    get_user_input("Enter Username:", chat_user, 32);
    get_user_input("Enter Password:", chat_pass, 32);

    // 3. Connect WiFi on Top Screen
    consoleSelect(&topScreen);
    printf("DSi Chat: %s\n", chat_user);
    printf("Connecting WiFi...\n");

    if (!Wifi_InitDefault(WFC_CONNECT)) {
        printf("WiFi failed!\n");
        while (1) swiWaitForVBlank();
    }

    while (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED) {
        swiWaitForVBlank();
    }

    struct in_addr ip = Wifi_GetIPInfo(NULL, NULL, NULL, NULL);
    printf("IP: %s\n", inet_ntoa(ip));

    // 4. Socket Connection
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(SERVER_PORT);
    server.sin_addr.s_addr = inet_addr(SERVER_IP);

    printf("Connecting to bridge...\n");
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("Bridge offline\n");
        while (1) swiWaitForVBlank();
    }

    // Set non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    // 5. Send Authentication
    char loginCmd[256];
    snprintf(loginCmd, sizeof(loginCmd), "LOGIN|%s|%s\n", chat_user, chat_pass);
    send(sock, loginCmd, strlen(loginCmd), 0);

    bool loggedIn = false;
    char netBuffer[1024];

    // 6. Main Loop
    while (1) {
        scanKeys();
        int pressed = keysDown();

        // Send a test message
        if (loggedIn && (pressed & KEY_A)) {
            char msg[] = "MSG|Hello from DSi!\n";
            send(sock, msg, strlen(msg), 0);
            printf("Sent message!\n");
        }

        // Receive logic
        int bytes = recv(sock, netBuffer, sizeof(netBuffer) - 1, 0);
        if (bytes > 0) {
            netBuffer[bytes] = '\0';
            if (!loggedIn && strstr(netBuffer, "OK|LOGIN")) {
                loggedIn = true;
                printf("Auth Success!\nPress A to chat.\n");
            } else {
                printf("%s", netBuffer); // Print incoming chat
            }
        }

        swiWaitForVBlank();
    }

    return 0;
}