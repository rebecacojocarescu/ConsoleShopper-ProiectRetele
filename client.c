#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>

int sd; // Declarat global pentru acces din thread

// Funcție care va rula în thread-ul de citire mesaje
void* citire_mesaje(void* arg) {
    char buf[1024];
    while (1) {
        bzero(buf, sizeof(buf));
        int n = read(sd, buf, sizeof(buf) - 1);
        if (n <= 0) {
            perror("[client] Eroare la citirea de la server sau conexiunea s-a inchis.");
            pthread_exit(NULL);
        }
        buf[n] = '\0';
        //printf("\nRaspuns de la server: %s\n", buf);
        //fflush(stdout);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server;
    char buf[1024], username[100], password[100], comanda[100];

    if (argc != 3) {
        printf("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
        return errno;
    }

    int port = atoi(argv[2]);
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Eroare la socket().\n");
        return errno;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(argv[1]);
    server.sin_port = htons(port);

    if (connect(sd, (struct sockaddr*)&server, sizeof(struct sockaddr)) == -1) {
        perror("[client] Eroare la connect().\n");
        return errno;
    }

    // Creare thread pentru citirea mesajelor de la server
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, citire_mesaje, NULL) != 0) {
        perror("Eroare la crearea thread-ului de citire.");
        return errno;
    }

    while (1) {
        printf("Introduceti comanda [login, produse, logout, add to cart, view cart]: ");
        fflush(stdout);
        bzero(comanda, sizeof(comanda));
        fgets(comanda, sizeof(comanda), stdin);
        comanda[strcspn(comanda, "\n")] = 0;

        if (write(sd, comanda, strlen(comanda)) < 0) {
            perror("[client] Eroare la write() spre server.\n");
            return errno;
        }

        if (strcmp(comanda, "login") == 0) {
            printf("Introduceti username: ");
            fflush(stdout);
            bzero(username, sizeof(username));
            fgets(username, sizeof(username), stdin);
            username[strcspn(username, "\n")] = 0;
            write(sd, username, strlen(username));

            printf("Introduceti password: ");
            fflush(stdout);
            bzero(password, sizeof(password));
            fgets(password, sizeof(password), stdin);
            password[strcspn(password, "\n")] = 0;
            write(sd, password, strlen(password));

            // Citirea raspunsului de autentificare de la server
            int n = read(sd, buf, sizeof(buf) - 1);
            if (n <= 0) {
                perror("[client] Eroare la citirea raspunsului de la server.\n");
                return errno;
            }
            buf[n] = '\0';
            printf("%s\n", buf);
        }

        else if (strcmp(comanda, "logout") == 0) {
            // Log out
            printf("Logged out.\n");
            write(sd, comanda, strlen(comanda));  // Trimite logout serverului
            break;
        }

        else if (strncmp(comanda, "add to cart ", 12) == 0) {
            // Adăugarea unui produs în coș
            printf("Adăugare produs în coș...\n");
            write(sd, comanda, strlen(comanda));  // Trimite comanda de adăugare în coș

            // Așteptă răspuns de confirmare din partea serverului
            int n = read(sd, buf, sizeof(buf) - 1);
            if (n <= 0) {
                perror("[client] Eroare la citirea raspunsului de la server.\n");
                return errno;
            }
            buf[n] = '\0';
            printf("%s\n", buf);
        }

        else if (strcmp(comanda, "view cart") == 0) {
            // Vizualizarea coșului
            
            write(sd, comanda, strlen(comanda));  // Trimite comanda de vizualizare coș

            // Așteptă răspunsul cu conținutul coșului
            int n = read(sd, buf, sizeof(buf) - 1);
            if (n <= 0) {
                perror("[client] Eroare la citirea raspunsului de la server.\n");
                return errno;
            }
            buf[n] = '\0';
            printf("Coșul de cumpărături: %s\n", buf);
        }

        else if (strcmp(comanda, "produse") == 0) {
            // Cerere pentru afișarea produselor
            write(sd, comanda, strlen(comanda));  // Trimite comanda de vizualizare produse

            // Așteptă răspunsul cu lista de produse
            int n = read(sd, buf, sizeof(buf) - 1);
            if (n <= 0) {
                perror("[client] Eroare la citirea raspunsului de la server.\n");
                return errno;
            }
            buf[n] = '\0';
            printf("Produse disponibile: %s\n", buf);
        }

        else {
            printf("Comanda necunoscuta. Incercati din nou.\n");
        }
    }

    close(sd);
    pthread_cancel(thread_id); // Oprirea thread-ului
    pthread_join(thread_id, NULL); // Așteptarea finalizării thread-ului

    return 0;
}