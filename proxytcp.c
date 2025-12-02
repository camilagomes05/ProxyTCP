#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>
#include <time.h>
#include <netinet/tcp.h>

#define BUFFER_SIZE 4096 //bytes
#define TARGET_BANDWIDTH_BYTES_SEC 1250000  //10 Mbps = 1.25 MB/s

//Políticas Dinâmicas (Pacing e Ajuste de Buffers)
void aplicar_politicas_dinamicas(int socket_fd, struct tcp_info *info, const char *socket_name) {

    //Ajuste de Buffers Baseado em RTT
    double rtt_sec = (double)info->tcpi_rtt / 1000000.0; //Converte o RTT em segundos
    int ideal_buffer = (int)(TARGET_BANDWIDTH_BYTES_SEC * rtt_sec); //Calcula buffer ideal para evitar lentidão
    if (ideal_buffer < 32768) ideal_buffer = 32768;  //Mínimo de 32 KB para evitar ociosidade
    if (ideal_buffer > 4194304) ideal_buffer = 4194304;  //Máximo de 4 MB para evitar desperdício 

    //Aplica SNDBUF (Envio)
    if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, &ideal_buffer, sizeof(ideal_buffer)) < 0) { 
        perror("Setsockopt SNDBUF");
    }
    //Aplica RCVBUF (Recebimento)
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &ideal_buffer, sizeof(ideal_buffer)) < 0) {
        perror("Setsockopt RCVBUF");
    }

    //TCP Pacing (Limitação de Taxa)
    unsigned int pacing_rate = TARGET_BANDWIDTH_BYTES_SEC; //Limite de velocidade
    if (setsockopt(socket_fd, SOL_SOCKET, SO_MAX_PACING_RATE, &pacing_rate, sizeof(pacing_rate)) < 0) { 
        //Não adicionei nada, caso dê erro, a otimização continua sem o Pacing
    }
}

//Monitoramento e Chamada de Otimização
void monitorar_conexao(int socket_fd, const char* socket_name, FILE *log_file, double timestamp, int otimizacao_ativa) {
    struct tcp_info info;
    socklen_t info_len = sizeof(info); //Guarda o tamanho da variável info

    if (getsockopt(socket_fd, IPPROTO_TCP, TCP_INFO, &info, &info_len) == 0) { //Se retornar zero, retorna os dados
        
        if (otimizacao_ativa) {//Se a otimização estar em 1
            aplicar_politicas_dinamicas(socket_fd, &info, socket_name);
        }

        //Exibe as Métricas no Terminal
        printf("\n--- [%s Metrics] ---\n", socket_name);
        printf("  RTT: %.3f ms\n", (double)info.tcpi_rtt / 1000.0);
        printf("  Retransmissões: %u\n", info.tcpi_retrans);
        printf("  CWND: %u | SSTHRESH: %u\n", info.tcpi_snd_cwnd, info.tcpi_snd_ssthresh);
        if(otimizacao_ativa) printf("  [Políticas Ativas]: Pacing + Buffer Adaptativo\n"); //Se a otimização estiver ativa
        printf("---------------------------------------\n");

        //Registra no Log CSV
        fprintf(log_file, "%.3f,%s,%.3f,%.3f,%u,%u,%u,N/A\n",
                timestamp,
                (strcmp(socket_name, "CLIENTE -> SERVIDOR") == 0) ? "C_S" : "S_C", 
                (double)info.tcpi_rtt / 1000.0,
                (double)info.tcpi_rttvar / 1000.0,
                info.tcpi_retrans,
                info.tcpi_snd_cwnd,
                info.tcpi_snd_ssthresh);
        fflush(log_file);//Força o SO a salvar os dados mesmo se travar ou dar Ctrl+C
    } else {//Senão, retorna erro
        perror("getsockopt TCP_INFO");
    }
}

//Função Principal de Conexão
void handle_connection(int client_sock, int server_sock, int otimizacao_ativa, FILE *log_file) {
    fd_set read_fds; //Lista de sockets
    int max_fd; //Quant máxima de sockets
    char buffer[BUFFER_SIZE]; //Array para guardar os dados que o proxy recebe, para depois enviar
    int nbytes; //Monitorar conexão
    
    struct timespec connection_start_time, throughput_start_time, current_time; //Calcular Timestamp, momento de inicio do throughput e horário atual
    double conn_elapsed_sec, throughput_elapsed_sec; //Hora Agora - Hora Inicial
    long total_bytes_cli = 0; //Total de bytes do cliente
    long total_bytes_srv = 0; //Total de bytes do servidor
    
    clock_gettime(CLOCK_MONOTONIC, &connection_start_time); //Crônometro que inicia no momento da conexão
    clock_gettime(CLOCK_MONOTONIC, &throughput_start_time); //Crônometro que inicia o throughput

    max_fd = (client_sock > server_sock) ? client_sock : server_sock; //Maior número entre o socket do Cliente e o socket do Servidor
    printf("Proxy: Iniciando encaminhamento de dados...\n");

    while (1) {
        FD_ZERO(&read_fds); //Limpa as listas anteriores de socket
        FD_SET(client_sock, &read_fds); //Adiciona o socket do cliente na lista
        FD_SET(server_sock, &read_fds); //Adiciona o socket do servidor na lista

        struct timeval timeout;
        timeout.tv_sec = 1;  //Timeout de 1 segundo para check de atividade
        timeout.tv_usec = 0;
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout); //Ativa o proxy quando o cliente e/ou o servidor enviar dados ou a cada 1s
        
        if (activity < 0) {
            perror("select");
            break;
        }

        //Calculo de timestamp
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        conn_elapsed_sec = (current_time.tv_sec - connection_start_time.tv_sec) + 
                           (current_time.tv_nsec - connection_start_time.tv_nsec) / 1000000000.0;

        //Cliente
        if (FD_ISSET(client_sock, &read_fds)) {
            nbytes = recv(client_sock, buffer, sizeof(buffer), 0); //Lê os dados enviados 
            if (nbytes <= 0) break; //Se for 0 ou erro, fecha a conexão
            total_bytes_cli += nbytes; //Contabiliza para o cálculo de Mbps
            if (send(server_sock, buffer, nbytes, 0) < 0) break; //Repassa para o Servidor
            
            //Monitora e Otimiza
            monitorar_conexao(server_sock, "CLIENTE -> SERVIDOR", log_file, conn_elapsed_sec, otimizacao_ativa);
        }

        //Servidor (Mesma coisa do cliente)
        if (FD_ISSET(server_sock, &read_fds)) {
            nbytes = recv(server_sock, buffer, sizeof(buffer), 0);
            if (nbytes <= 0) break;
            total_bytes_srv += nbytes;
            if (send(client_sock, buffer, nbytes, 0) < 0) break;

            //Monitora e Otimiza
            monitorar_conexao(client_sock, "SERVIDOR -> CLIENTE", log_file, conn_elapsed_sec, otimizacao_ativa);
        }
        
        //Log de Throughput (a cada 1s)
        throughput_elapsed_sec = (current_time.tv_sec - throughput_start_time.tv_sec) + 
                                 (current_time.tv_nsec - throughput_start_time.tv_nsec) / 1000000000.0; //Calcula quanto tempo passou desde o último log

        if (throughput_elapsed_sec >= 1.0) {
            double throughput_cli = (total_bytes_cli * 8.0) / throughput_elapsed_sec / 1000000.0;  //Calcula velocidade de envio do cliente
            double throughput_srv = (total_bytes_srv * 8.0) / throughput_elapsed_sec / 1000000.0; //Calcula velocidade de envio do servidor

            //Printa os dados no prompt
            printf("\n--- [Throughput (Mbps)] ---\n");
            printf("  Cli -> Srv: %.4f Mbps\n", throughput_cli);
            printf("  Srv -> Cli: %.4f Mbps\n", throughput_srv);
            printf("---------------------------\n");

            //Grava no CSV
            fprintf(log_file, "%.3f,C_S,N/A,N/A,N/A,N/A,N/A,%.4f\n", conn_elapsed_sec, throughput_cli);
            fprintf(log_file, "%.3f,S_C,N/A,N/A,N/A,N/A,N/A,%.4f\n", conn_elapsed_sec, throughput_srv);
            fflush(log_file); 
            
            //Zera os contadores para o próximo segundo
            total_bytes_cli = 0;
            total_bytes_srv = 0;
            clock_gettime(CLOCK_MONOTONIC, &throughput_start_time);
        }
    } 
}

int main(int argc, char *argv[]) {

    int listen_sock, client_sock, server_sock; //Cria socket passivo, socket cliente e socket servidor
    struct sockaddr_in proxy_addr, server_addr, client_addr; //Cria espaços de memória para adicionar endereços
    socklen_t client_len = sizeof(client_addr); //Tamanho da memória para adicionar os endereços 
    int otimizacao_ativa = 0; 
    
    //Cria arquivo com logs
    FILE *log_file = fopen("log.csv", "w");
    if (!log_file) { perror("Log file"); exit(1); }
    fprintf(log_file, "Timestamp,Direction,RTT_ms,RTTvar_ms,Retransmissions,CWND,SSTHRESH,Throughput_Mbps\n");
    fflush(log_file); 
    printf("[INFO] Log em log.csv\n");

    //Busca pela palavra "otimizar" no proxy no prompt
    if (argc < 4 || argc > 5) {
        fprintf(stderr, "Uso: %s <porta_proxy> <host_real> <porta_real> [--otimizar]\n", argv[0]); 
        exit(1);
    }
    
    // Se possuir a palavra "otimizar", ativa as políticas dinamicas
    if (argc == 5 && strcmp(argv[4], "--otimizar") == 0) {
        otimizacao_ativa = 1;
        printf("[INFO] Políticas Dinâmicas (Pacing + Buffer Adaptativo) ATIVADAS.\n");
    } else { // Se não possuir, prossegue sem as políticas
        printf("[INFO] Otimizações DESATIVADAS.\n");
    }
    
    // Cria as variáveis para portas e host, adiciona os argumentos digitados em cada um
    int proxy_port = atoi(argv[1]);
    char *server_host = argv[2];
    int server_port = atoi(argv[3]);

    listen_sock = socket(AF_INET, SOCK_STREAM, 0); // Cria o socket utilizando TCP e IPv4
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); //Permite a conexão no proxy de forma imediata, mesmo que a conexão seja encerrada recentemente

    memset(&proxy_addr, 0, sizeof(proxy_addr)); //Preenche com zero aqui, limpando a memória
    proxy_addr.sin_family = AF_INET; //IPv4
    proxy_addr.sin_addr.s_addr = INADDR_ANY; //IP da máquina
    proxy_addr.sin_port = htons(proxy_port); //Porta do proxy

    if (bind(listen_sock, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr)) < 0) { perror("bind"); exit(1); } //Associa o socket ao endereço e porta
    if (listen(listen_sock, 5) < 0) { perror("listen"); exit(1); } //Transforma em socket passivio

    printf("Proxy aguardando na porta %d...\n", proxy_port);

    //Conexão
    while (1) {
        client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len); //Aguarda conexão do cliente
        if (client_sock < 0) continue; //Se deu erro ao conectar, espero o próximo cliente
        
        server_sock = socket(AF_INET, SOCK_STREAM, 0); //Assim que o cliente conecta, cria um novo socket ativo para conectar ao servidor
        memset(&server_addr, 0, sizeof(server_addr)); //Preenche com zero aqui, limpando a memória
        server_addr.sin_family = AF_INET; //IPv4
        server_addr.sin_port = htons(server_port); //Define porta de destino
        inet_pton(AF_INET, server_host, &server_addr.sin_addr); //Converte IP para binário

        //Conecta com o servidor
        if (connect(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) { //Se a conexão com o servidor falhar
            perror("connect server"); //Motivo do erro
            close(client_sock); //Encerra com o cliente
            continue; //Retorna para aguardar uma nova conexão com cliente
        }

        handle_connection(client_sock, server_sock, otimizacao_ativa, log_file);

        //Encerrar conexão de ambos os lados
        close(client_sock);
        close(server_sock);
        printf("Conexão encerrada.\n\n");
    }
    fclose(log_file);
    close(listen_sock);
    return 0;
}
