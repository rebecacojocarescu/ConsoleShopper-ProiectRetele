#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include "sqlite3.h"

#define PORT 4024

sqlite3 *db;

int validate_user(const char *username, const char *password) {
    const char *sql_user_check = "SELECT id FROM users WHERE username = ?";
    const char *sql_pass_check = "SELECT id FROM users WHERE username = ? AND password = ?";
    sqlite3_stmt *stmt;

    // Verificare existenta username
    if (sqlite3_prepare_v2(db, sql_user_check, -1, &stmt, 0) != SQLITE_OK) {
        fprintf(stderr, "Eroare SQL prepare: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    int user_found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);

    if (!user_found) {
        printf("Username '%s' nu exista in baza de date.\n", username);
        return 0;
    } else {
        printf("Username '%s' gasit in baza de date.\n", username);
    }

    // Verificare existenta combinatie username/parola
    if (sqlite3_prepare_v2(db, sql_pass_check, -1, &stmt, 0) != SQLITE_OK) {
        fprintf(stderr, "Eroare SQL prepare: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    int login_successful = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);

    if (login_successful) {
        printf("Parola corecta pentru username '%s'.\n", username);
        return 1;
    } else {
        printf("Parola incorecta pentru username '%s'.\n", username);
        return 0;
    }
}

struct cart_item {
    char product[100];
    double price;
    int quantity;
};


struct thData {
    int idThread;
    int cl;
    struct cart_item cart[50]; //max 50 produse in cos
    int cart_size;
};

void products_list(int client)
{
    const char *sql = "SELECT type, product, price FROM products ORDER BY type";
    sqlite3_stmt *stmt;
    
    if(sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK)
    {
        fprintf(stderr, "Eroare la interogarea SQL: %s\n", sqlite3_errmsg(db));
        write(client, "Eroare la obtinerea listei de produse.\n", strlen("Eroare la obtinerea listei de produse.\n"));
    }

    char current_type[100] = "";
    int first_type = 1;

    while(sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char *type = (const char *)sqlite3_column_text(stmt, 0);
        const char *product = (const char *)sqlite3_column_text(stmt, 1);
        double price = sqlite3_column_double(stmt,2);

        if(strcmp(current_type, type) != 0) {
            if(!first_type) {
                write(client, "\n", 1);
            }
            snprintf(current_type, sizeof(current_type), "%s", type);
            write(client, "Categorie: ", strlen("Categorie: "));
            write(client, current_type, strlen(current_type));
            write(client, "\n", 1);
            first_type = 0;
        }

        //trimite nume produs
        write(client, "Product: ", strlen("Product: "));
        write(client, product, strlen(product));

        //trimitere pret produs
        char pr[100];
        sprintf(pr, "| Pret: %.2f\n", price);
        write(client, pr, strlen(pr));
    }
    sqlite3_finalize(stmt);

    write(client, "<END>.\n", strlen("<END>.\n"));
}

void add_to_cart(struct thData *td, const char *product_name) {
    printf("[DEBUG] Adaugare produs in cos: %s\n", product_name);

    const char *sql = "SELECT product, price, in_stock FROM products WHERE product = ?";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        fprintf(stderr, "Eroare SQL prepare: %s\n", sqlite3_errmsg(db));
        write(td->cl, "Eroare la verificarea produsului.\n", strlen("Eroare la verificarea produsului.\n"));
        return;
    }

    sqlite3_bind_text(stmt, 1, product_name, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *db_product = (const char *)sqlite3_column_text(stmt, 0);
        double price = sqlite3_column_double(stmt, 1);
        int in_stock = sqlite3_column_int(stmt, 2);

        if (in_stock > 0) {
            int found = 0;

            // Verifică dacă produsul există deja în coș
            for (int i = 0; i < td->cart_size; i++) {
                if (strcmp(td->cart[i].product, db_product) == 0) {
                    td->cart[i].quantity++;
                    found = 1;
                    break;
                }
            }

            if (!found) {
                snprintf(td->cart[td->cart_size].product, sizeof(td->cart[td->cart_size].product), "%s", db_product);
                td->cart[td->cart_size].price = price;
                td->cart[td->cart_size].quantity = 1;
                td->cart_size++;
            }

            // Actualizează stocul în baza de date
            const char *update_stock_sql = "UPDATE products SET in_stock = in_stock - 1 WHERE product = ?";
            sqlite3_stmt *update_stmt;
            if (sqlite3_prepare_v2(db, update_stock_sql, -1, &update_stmt, 0) == SQLITE_OK) {
                sqlite3_bind_text(update_stmt, 1, db_product, -1, SQLITE_STATIC);
                sqlite3_step(update_stmt);
                sqlite3_finalize(update_stmt);
            }

            write(td->cl, "Produs adaugat in cos.\n", strlen("Produs adaugat in cos.\n"));
        } else {
            write(td->cl, "Produsul nu este in stoc.\n", strlen("Produsul nu este in stoc.\n"));
        }
    } else {
        write(td->cl, "Produsul nu exista in baza de date.\n", strlen("Produsul nu exista in baza de date.\n"));
    }

    sqlite3_finalize(stmt);
}

// Functia pentru a vizualiza cosul de cumparaturi
void view_cart(struct thData *td) {
    if (td->cart_size == 0) {
        write(td->cl, "Cosul este gol.\n", strlen("Cosul este gol.\n"));
        return;
    }

    double total = 0;
    char response[256];

    for (int i = 0; i < td->cart_size; i++) {
        snprintf(response, sizeof(response), "Produs: %s | Cantitate: %d | Pret: %.2f\n",
                 td->cart[i].product, td->cart[i].quantity, td->cart[i].price);
        write(td->cl, response, strlen(response));
        total += td->cart[i].price * td->cart[i].quantity;
    }

    snprintf(response, sizeof(response), "Total: %.2f\n", total);
    write(td->cl, response, strlen(response));
}


void *treat(void *arg) {
    struct thData tdL = *((struct thData *)arg);
    pthread_detach(pthread_self());
    char buf[256];
    int logged_in = 0;

    while (1) {
        bzero(buf, sizeof(buf));
        int n = read(tdL.cl, buf, sizeof(buf) - 1);
        
        if (n <= 0) {
            close(tdL.cl);
            printf("[server][thread %d] Clientul a inchis conexiunea.\n", tdL.idThread);
            return NULL;
        }

        buf[n] = '\0';
        printf("[server][thread %d] Comanda primita: %s\n", tdL.idThread, buf);

        if (strcmp(buf, "login") == 0) {
            char username[100], password[100];

            // Cerere username
            write(tdL.cl, "Username: ", strlen("Username: "));
            n = read(tdL.cl, username, sizeof(username) - 1);
            if (n <= 0) {
                close(tdL.cl);
                return NULL;
            }
            username[n] = '\0';

            // Cerere password
            write(tdL.cl, "Password: ", strlen("Password: "));
            n = read(tdL.cl, password, sizeof(password) - 1);
            if (n <= 0) {
                close(tdL.cl);
                return NULL;
            }
            password[n] = '\0';

            // Validare utilizator
            if (validate_user(username, password)) {
                write(tdL.cl, "Login successful.\n", strlen("Login successful.\n"));
                logged_in = 1;
            } else {
                write(tdL.cl, "Invalid username or password.\n", strlen("Invalid username or password.\n"));
            }

        } else if (strcmp(buf, "logout") == 0) {
            write(tdL.cl, "Logged out.\n", strlen("Logged out.\n"));
            close(tdL.cl);
            return NULL;
        }

        else if (strcmp(buf, "produse") == 0) {
            if (logged_in) {
                products_list(tdL.cl);
            } else {
                write(tdL.cl, "Please log in first.\n", strlen("Please log in first.\n"));
            }
        }

        else if (strncmp(buf, "add to cart", 11) == 0) {
            if (logged_in) {
                char product_name[100];
                sscanf(buf + 12, "%99[^\n]", product_name);

                const char *sql_check_product = "SELECT product, price, in_stock FROM products WHERE product = ?";
                sqlite3_stmt *stmt;

                if (sqlite3_prepare_v2(db, sql_check_product, -1, &stmt, 0) != SQLITE_OK) {
                    fprintf(stderr, "Eroare SQL prepare: %s\n", sqlite3_errmsg(db));
                    write(tdL.cl, "Eroare la verificarea produsului.\n", strlen("Eroare la verificarea produsului.\n"));
                    continue;
                }

                sqlite3_bind_text(stmt, 1, product_name, -1, SQLITE_STATIC);

                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char *product = (const char *)sqlite3_column_text(stmt, 0);
                    double price = sqlite3_column_double(stmt, 1);
                    int in_stock = sqlite3_column_int(stmt, 2);

                    if (in_stock > 0) {
                        // Adaugare produs in coș
                        snprintf(tdL.cart[tdL.cart_size].product, sizeof(tdL.cart[tdL.cart_size].product), "%s", product);
                        tdL.cart[tdL.cart_size].price = price;
                        tdL.cart[tdL.cart_size].quantity = 1;
                        tdL.cart_size++;

                        write(tdL.cl, "Produs adaugat in cos.\n", strlen("Produs adaugat in cos.\n"));
                    } else {
                        write(tdL.cl, "Produs indisponibil in stoc.\n", strlen("Produs indisponibil in stoc.\n"));
                    }
                } else {
                    write(tdL.cl, "Produsul nu exista.\n", strlen("Produsul nu exista.\n"));
                }

                sqlite3_finalize(stmt);
            } else {
                write(tdL.cl, "Please log in first.\n", strlen("Please log in first.\n"));
            }
        }

        else if (strcmp(buf, "view cart") == 0) {
        if (logged_in) {
            view_cart(&tdL);  // Afisează produsele din cos
        } else {
            write(tdL.cl, "Please log in first.\n", strlen("Please log in first.\n"));
        }
}


        else {
            write(tdL.cl, "Unknown command.\n", strlen("Unknown command.\n"));
        }
    }
}


int main() {
    if (sqlite3_open("ConsoleShopper.db", &db) != SQLITE_OK) {
        fprintf(stderr, "Nu se poate deschide baza de date: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    int sd;
    struct sockaddr_in server, from;
    pthread_t th[100];
    int i = 0;

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("[server] Eroare la socket().");
        return errno;
    }

    int on = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1) {
        perror("[server] Eroare la bind().");
        return errno;
    }

    if (listen(sd, 5) == -1) {
        perror("[server] Eroare la listen().");
        return errno;
    }

    printf("[server] Asteptam conexiuni pe portul %d...\n", PORT);

    while (1) {
        int client;
        struct thData *td;
        int length = sizeof(from);

        if ((client = accept(sd, (struct sockaddr *)&from, &length)) < 0) {
            perror("[server] Eroare la accept().");
            continue;
        }

        td = (struct thData *)malloc(sizeof(struct thData));
        td->idThread = i++;
        td->cl = client;

        pthread_create(&th[i], NULL, &treat, td);
    }

    sqlite3_close(db);
    return 0;
}