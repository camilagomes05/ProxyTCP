#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>
#include <netinet/tcp.h> // ETAPA 2: Para a estrutura tcp_info
#include <time.h>        // ETAPA 2: Para o cálculo de throughput

#define BUFFER_SIZE 4096

/**
 * ETAPA 2: Função para coletar e exibir métricas TCP
 */
void monitorar_conexao(int socket_fd, const char* socket_name) {
    struct tcp_info info; // Estrutura que armazena as métricas
    socklen_t info_len = sizeof(info);

    // Esta é a chamada de sistema que pede ao Kernel as métricas TCP
    if (getsockopt(socket_fd, IPPROTO_TCP, TCP_INFO, &info, &info_len) == 0) {
        
        printf("\n--- [%s Metrics] ---\n", socket_name);
        
        // RTT e variação (em microssegundos, convertemos para milissegundos) [cite: 29]
        printf("  RTT: %.3f ms\n", (double)info.tcpi_rtt / 1000.0);
        printf("  RTT Var: %.3f ms\n", (double)info.tcpi_rttvar / 1000.0);
        
        // Taxa de retransmissões (total) [cite: 33]
        printf("  Retransmissões: %u\n", info.tcpi_retrans);
        
        // Janela de congestionamento (em pacotes) [cite: 34]
        printf("  CWND (pacotes): %u\n", info.tcpi_snd_cwnd);
        
        // Limiar (ssthresh) [cite: 34]
        printf("  SSTHRESH: %u\n", info.tcpi_snd_ssthresh);
        printf("---------------------------------------\n");
        
    } else {
        // Se falhar, apenas imprime o erro e continua
        perror("getsockopt TCP_INFO");
    }
}


/**
 * ETAPA 1: Função principal para lidar com a conexão do proxy.
 * ETAPA 2: Modificada para incluir cálculo de Throughput.
 */
void handle_connection(int client_sock, int server_sock) {
    fd_set read_fds;
    int max_fd;
    char buffer[BUFFER_SIZE];
    int nbytes;

    // --- ADIÇÃO PARA THROUGHPUT ---
    struct timespec start_time, current_time;
    double elapsed_time;
    long total_bytes_cli = 0; // Bytes do Cliente -> Servidor
    long total_bytes_srv = 0; // Bytes do Servidor -> Cliente
    
    // Pega o tempo inicial
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    // --- FIM DA ADIÇÃO ---


    max_fd = (client_sock > server_sock) ? client_sock : server_sock;
    printf("Proxy: Iniciando encaminhamento de dados...\n");

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(client_sock, &read_fds);
        FD_SET(server_sock, &read_fds);

        // --- MODIFICAÇÃO PARA TIMEOUT (Throughput) ---
        // Faz o select() ter um timeout de 1 segundo
        // para que possamos imprimir o throughput periodicamente
        struct timeval timeout;
        timeout.tv_sec = 1; // 1 segundo
        timeout.tv_usec = 0;
        
        // select() agora retorna 0 se o timeout ocorrer
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            perror("select");
            break;
        }
        // --- FIM DA MODIFICAÇÃO ---

        // 1. Verificar dados vindo do CLIENTE
        if (FD_ISSET(client_sock, &read_fds)) {
            nbytes = recv(client_sock, buffer, sizeof(buffer), 0);
            if (nbytes <= 0) {
                if (nbytes == 0) {
                    printf("Proxy: Cliente desconectou.\n");
                } else {
                    perror("recv client");
                }
                break; 
            }

            // --- ADIÇÃO PARA THROUGHPUT ---
            total_bytes_cli += nbytes;
            // --- FIM DA ADIÇÃO ---

            // Encaminha dados para o SERVIDOR
            if (send(server_sock, buffer, nbytes, 0) < 0) {
                perror("send server");
                break;
            }
            
            // --- INÍCIO ETAPA 2 (Métricas) ---
            monitorar_conexao(server_sock, "CLIENTE -> SERVIDOR");
            // --- FIM ETAPA 2 ---
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
                break;
            }

            // --- ADIÇÃO PARA THROUGHPUT ---
            total_bytes_srv += nbytes;
            // --- FIM DA ADIÇÃO ---

            // Encaminha dados para o CLIENTE
            if (send(client_sock, buffer, nbytes, 0) < 0) {
                perror("send client");
                break;
            }

            // --- INÍCIO ETAPA 2 (Métricas) ---
            monitorar_conexao(client_sock, "SERVIDOR -> CLIENTE");
            // --- FIM ETAPA 2 ---
        }

        // --- ADIÇÃO PARA THROUGHPUT (Cálculo periódico) ---
        // Pega o tempo atual
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        
        // Calcula o tempo decorrido em segundos
        elapsed_time = (current_time.tv_sec - start_time.tv_sec);
        elapsed_time += (current_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

        // Se passou pelo menos 1 segundo, calcula e imprime o throughput 
        if (elapsed_time >= 1.0) {
            // (bytes * 8 bits) / tempo / 1_000_000 = Megabits por segundo (Mbps)
            double throughput_cli = (total_bytes_cli * 8.0) / elapsed_time / 1000000.0; 
            double throughput_srv = (total_bytes_srv * 8.0) / elapsed_time / 1000000.0; 

            printf("\n--- [Throughput Médio (Mbps)] ---\n");
            printf("  Cli -> Srv: %.4f Mbps\n", throughput_cli);
            printf("  Srv -> Cli: %.4f Mbps\n", throughput_srv);
            printf("----------------------------------\n");

            // Reseta o contador para o próximo intervalo de 1 segundo
            total_bytes_cli = 0;
            total_bytes_srv = 0;
            clock_gettime(CLOCK_MONOTONIC, &start_time);
        }
        // --- FIM DA ADIÇÃO ---

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

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        perror("socket listen");
        exit(EXIT_FAILURE);
    }

    // Permite reusar o endereço imediatamente (útil para testes)
    int opt = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
    }

    memset(&proxy_addr, 0, sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = INADDR_ANY;
    proxy_addr.sin_port = htons(proxy_port);

    if (bind(listen_sock, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listen_sock, 5) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Proxy TCP aguardando conexões na porta %d...\n", proxy_port);

    while (1) {
        client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("accept");
            continue; 
        }
        printf("Proxy: Cliente conectado de %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock < 0) {
            perror("socket server");
            close(client_sock);
            continue;
        }

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        if (inet_pton(AF_INET, server_host, &server_addr.sin_addr) <= 0) {
            perror("inet_pton server host");
            close(client_sock);
            close(server_sock);
            continue;
        }

        if (connect(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("connect to real server");
            close(client_sock);
            close(server_sock);
            continue;
        }
        printf("Proxy: Conectado ao servidor real %s:%d\n", server_host, server_port);

        handle_connection(client_sock, server_sock);

        close(client_sock);
        close(server_sock);
        printf("Proxy: Conexão encerrada. Aguardando novo cliente...\n\n");
    }

    close(listen_sock);
    return 0;
}
