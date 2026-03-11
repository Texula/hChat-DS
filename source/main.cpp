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
#include <sys/ioctl.h>

#define SERVER_PORT 5050
#define SERVER_IP "37.143.163.56"

char chat_user[32] = {0};
char chat_pass[32] = {0};

PrintConsole topScreen;
PrintConsole bottomScreen;

// Hardware cleanup for TWiLight Menu++ / nds-bootstrap
void sanitize_hardware(void) {
    REG_BG0HOFS_SUB = 0; REG_BG0VOFS_SUB = 0;
    REG_BG1HOFS_SUB = 0; REG_BG1VOFS_SUB = 0;
    REG_BG2HOFS_SUB = 0; REG_BG2VOFS_SUB = 0;
    REG_BG3HOFS_SUB = 0; REG_BG3VOFS_SUB = 0;

    REG_BG0HOFS = 0; REG_BG0VOFS = 0;
    REG_BG1HOFS = 0; REG_BG1VOFS = 0;
    REG_BG2HOFS = 0; REG_BG2VOFS = 0;
    REG_BG3HOFS = 0; REG_BG3VOFS = 0;
}

void get_user_input(const char* prompt, char* buffer, int max_len) {
    consoleSelect(&bottomScreen);
    printf("\x1b[2J"); // Clear window
    printf("\n%s\n> ", prompt);
    
    int index = 0;
    while(1) {
        swiWaitForVBlank();
        int c = keyboardUpdate();
        
        if (c == '\n') { 
            buffer[index] = '\0';
            break;
        } else if (c == '\b' && index > 0) {
            index--;
            printf("\b \b"); 
        } else if (c > 0 && index < max_len - 1) {
            buffer[index++] = (char)c;
            printf("%c", (char)c); 
        }
    }
}

int main(void) {
    sanitize_hardware();

    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);

    // TOP SCREEN: Incoming chat logs only!
    consoleInit(&topScreen, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
    
    // BOTTOM SCREEN: Typing Console (Top 6 rows)
    consoleInit(&bottomScreen, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);
    consoleSetWindow(&bottomScreen, 0, 0, 32, 6); 

    // BOTTOM SCREEN: Keyboard (Background 3, map base 20 to prevent visual tearing)
    Keyboard* kb = keyboardGetDefault();
    keyboardInit(kb, 3, BgType_Text4bpp, BgSize_T_256x256, 20, 0, false, true);
    bgSetScroll(kb->background, 0, 0); // Fix nds-bootstrap shifting
    keyboardShow();

    consoleSelect(&topScreen);
    printf("--- DSi Chat Setup ---\nConnecting to WiFi...\n");

    if (!Wifi_InitDefault(WFC_CONNECT)) {
        printf("WiFi failed!\n");
        while (1) swiWaitForVBlank();
    }
    while (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED) swiWaitForVBlank();
    
    printf("WiFi Connected!\n");

    get_user_input("Enter Username:", chat_user, 32);
    get_user_input("Enter Password:", chat_pass, 32);

    consoleSelect(&topScreen);
    printf("\nUser: %s\nConnecting to Server...\n", chat_user);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(SERVER_PORT);
    server.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("Bridge offline\n");
        while (1) swiWaitForVBlank();
    }

    // --- NON-BLOCKING SOCKET SETUP ---
    // dswifi heavily relies on ioctl for non-blocking states, fcntl alone is unreliable here!
    int nonBlock = 1;
    ioctl(sock, FIONBIO, &nonBlock); 
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);

    char loginCmd[256];
    snprintf(loginCmd, sizeof(loginCmd), "LOGIN|%s|%s\n", chat_user, chat_pass);
    send(sock, loginCmd, strlen(loginCmd), 0);

    bool loggedIn = false;
    char netBuffer[1024];
    
    char myInput[128] = {0};
    int inputLen = 0;

    // Reset bottom screen for typing mode
    consoleSelect(&bottomScreen);
    printf("\x1b[2J> "); 

    while (1) {
        // --- 1. HANDLE KEYBOARD TYPING ---
        int key = keyboardUpdate();
        if (key > 0) {
            consoleSelect(&bottomScreen); // Switch to typing window
            
            if (key == '\n') {
                if (loggedIn && inputLen > 0) {
                    char finalMsg[256];
                    snprintf(finalMsg, sizeof(finalMsg), "MSG|%s\n", myInput);
                    send(sock, finalMsg, strlen(finalMsg), 0);
                    
                    // We DO NOT print the message to topScreen here! 
                    // We wait for the server to echo it back so we know it actually sent.
                    
                    // Reset input buffer & clear bottom screen
                    memset(myInput, 0, sizeof(myInput));
                    inputLen = 0;
                    printf("\x1b[2J> "); 
                }
            } else if (key == '\b') {
                if (inputLen > 0) {
                    myInput[--inputLen] = '\0';
                    printf("\b \b");
                }
            } else if (inputLen < 100) {
                myInput[inputLen++] = (char)key;
                myInput[inputLen] = '\0';
                printf("%c", key);
            }
        }

        // --- 2. HANDLE NETWORK RECEIVE ---
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0; 

        // select() peeks to see if data is ready. Prevents the DS from freezing on recv!
        if (select(sock + 1, &readfds, NULL, NULL, &tv) > 0) {
            int bytes = recv(sock, netBuffer, sizeof(netBuffer) - 1, 0);
            
            if (bytes > 0) {
                netBuffer[bytes] = '\0';
                
                // Switch to Top Screen to print incoming chat data!
                consoleSelect(&topScreen); 
                
                if (!loggedIn && strstr(netBuffer, "OK|LOGIN")) {
                    loggedIn = true;
                    printf("\x1b[32m--- Auth Success! ---\x1b[39m\nYou can now type below.\n");
                } else if (loggedIn) {
                    // This prints incoming messages AND our own echoed messages
                    printf("%s\n", netBuffer); 
                }
            }
        }

        swiWaitForVBlank();
    }

    return 0;
}