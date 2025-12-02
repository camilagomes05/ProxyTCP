#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>

// Para monitoramento (Etapa 2) - precisa de #include <netinet/tcp.h>
// #include <netinet/tcp.h> 

#define BUFFER_SIZE 4096 // Um buffer maior para eficiência

/**
 * Função principal para lidar com a conexão do proxy.
 * Usa select() para multiplexar a E/S entre o cliente e o servidor.
 */
void handle_connection(int client_sock, int server_sock) {
    fd_set read_fds;
    int max_fd;
    char buffer[BUFFER_SIZE];
    int nbytes;

    // Determina o descritor de arquivo de maior valor para o select()
    max_fd = (client_sock > server_sock) ? client_sock : server_sock;

    printf("Proxy: Iniciando encaminhamento de dados...\n");

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(client_sock, &read_fds);
        FD_SET(server_sock, &read_fds);

        // Bloqueia até que haja dados para ler em um dos sockets
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        // 1. Verificar dados vindo do CLIENTE
        if (FD_ISSET(client_sock, &read_fds)) {
            nbytes = recv(client_sock, buffer, sizeof(buffer), 0);
            if (nbytes <= 0) {
                if (nbytes == 0) {
                    printf("Proxy: Cliente desconectou.\n");
                } else {
                    perror("recv client");
                }
                break; // Encerra o loop
            }

            // --- PONTO DE MONITORAMENTO/OTIMIZAÇÃO (Cliente -> Servidor) ---
            // TODO: Aqui você pode:
            // 1. Coletar métricas (getsockopt com TCP_INFO) 
            // 2. Aplicar otimizações (ex: TCP pacing) [cite: 44]
            // 3. Registrar dados em log 
            // -----------------------------------------------------------

            // Encaminha dados para o SERVIDOR
            if (send(server_sock, buffer, nbytes, 0) < 0) {
                perror("send server");
                break;
            }
        }

        // 2. Verificar dados vindo do SERVIDOR
        if (FD_ISSET(server_sock, &read_fds)) {
            nbytes = recv(server_sock, buffer, sizeof(buffer), 0);
            if (nbytes <= 0) {
                if (nbytes == 0) {
                    printf("Proxy: Servidor desconectou.\n");
                } else {
                    perror("recv server");
                }
                break; // Encerra o loop
            }

            // --- PONTO de MONITORAMENTO/OTIMIZAÇÃO (Servidor -> Cliente) ---
            // TODO: Aqui você pode:
            // 1. Coletar métricas [cite: 28]
            // 2. Aplicar otimizações (ex: delayed ACKs, ajuste de buffer) [cite: 43, 45]
            // 3. Registrar dados em log 
            // -----------------------------------------------------------

            // Encaminha dados para o CLIENTE
            if (send(client_sock, buffer, nbytes, 0) < 0) {
                perror("send client");
                break;
            }
        }
    } // fim do while(1)
}

int main(int argc, char *argv[]) {
    int listen_sock, client_sock, server_sock;
    struct sockaddr_in proxy_addr, server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    if (argc != 4) {
        fprintf(stderr, "Uso: %s <porta_proxy> <host_servidor_real> <porta_servidor_real>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int proxy_port = atoi(argv[1]);
    char *server_host = argv[2];
    int server_port = atoi(argv[3]);

    // 1. Criar socket de escuta do proxy
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        perror("socket listen");
        exit(EXIT_FAILURE);
    }

    // Configurar endereço do proxy
    memset(&proxy_addr, 0, sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = INADDR_ANY;
    proxy_addr.sin_port = htons(proxy_port);

    // 2. Bind do proxy na porta especificada
    if (bind(listen_sock, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    // 3. Listen (aguardando conexões do clienttcp.c)
    if (listen(listen_sock, 5) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Proxy TCP aguardando conexões na porta %d...\n", proxy_port);

    // Loop principal para aceitar clientes
    while (1) {
        // 4. Aceitar conexão do cliente
        client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("accept");
            continue; // Tenta o próximo cliente
        }
        printf("Proxy: Cliente conectado de %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // 5. Criar socket para conectar ao servidor real
        server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock < 0) {
            perror("socket server");
            close(client_sock);
            continue;
        }

        // Configurar endereço do servidor real
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        if (inet_pton(AF_INET, server_host, &server_addr.sin_addr) <= 0) {
            perror("inet_pton server host");
            close(client_sock);
            close(server_sock);
            continue;
        }

        // 6. Conectar ao servidor real
        if (connect(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("connect to real server");
            close(client_sock);
            close(server_sock);
            continue;
        }
        printf("Proxy: Conectado ao servidor real %s:%d\n", server_host, server_port);

        // 7. Chamar a função que gerencia a troca de dados
        //    (Esta implementação simples lida com um cliente por vez)
        //    (Uma implementação avançada usaria fork() ou threads aqui)
        handle_connection(client_sock, server_sock);

        // 8. Limpeza da conexão
        close(client_sock);
        close(server_sock);
        printf("Proxy: Conexão encerrada. Aguardando novo cliente...\n\n");
    }

    close(listen_sock);
    return 0;
}