Dados Gerais:
- Projeto: Proxy TCP
- Linguagem Utilizada: C
- Autora: Camila de Oliveira Gomes
- Disciplina: Redes de Computadores
- Ano: 2025


Este projeto implementa um Proxy TCP, possuindo como objetivo: 
- Intermediar entre Cliente e Servidor 
- Monitorar métricas (RTT estimado e variação (RTTVAR), Taxa de retransmissões, Janela de congestionamento (cwnd) e limiar de ssthresh, Throughput e goodput da conexão)
- Aplicar Otimizações Dinâmicas (TCP pacing e Ajuste de buffers)

Estrutura do projeto:
- Cliente TCP
- Servidor TCP
- Proxy TCP
- Gerador de gráficos


Requisitos para teste:
- Sistema Operacional: Linux
- Compilador: GCC
- Ferramentas de Rede: tc (Traffic Control) para simulação de cenários
- Python 3: Com bibliotecas pandas e matplotlib para geração de gráficos

Instalação das Dependências:
- sudo apt update
- sudo apt install build-essential iproute2 python3-pip
- sudo apt install python3-pandas python3-matplotlib


Compilação:
- Compila o Proxy: 
   gcc -o proxytcp proxytcp.c -lrt
- Compilar Cliente e Servidor:
   gcc -o clienttcp clienttcp.c
   gcc -o servertcp servertcp.c



Execução dos Cenários de Teste:
- São feitos apenas testes Com Otimização e Sem Otimização
- Já existem 2 arquivos para testes automáticos, não precisam ser criados
- Para facilitar, estes são os nomes dos arquivos de teste:
  trafego.txt (Arquivo de 5MB - Cenários 1 e 4)
  trafego2.txt (Arquivo de 50Kb - Cenários 2 e 3)
- Para cada teste, você precisará de 4 terminais abertos:
   Terminal 1: Servidor 
   Terminal 2: Proxy 
   Terminal 3: Cliente 
   Terminal 4: Controle de Rede

Cenário 1: Rede Ideal (Base) - Sem latência
- Servidor (Term. 1): ./servertcp 8080
- Proxy (Term. 2): ./proxytcp 9090 127.0.0.1 8080 (Sem Otimização)
- Cliente (Term. 3): cat trafego.txt | ./clienttcp 127.0.0.1 9090 
- Controle de Rede (Term. 4): Não será utilizado neste cenário
- Repita todo o processo novamente, porém com o proxy: ./proxytcp 9090 127.0.0.1 8080 --otimizar
* Como o Proxy já gera o arquivo log.txt de forma automatica contendo todos os dados, poderá renomear o log.csv gerado para log_base_sem.csv ou log_base_com.csv

Cenário 2: Latência 50ms com 1% Perda
- Controle de Rede (Term. 4): sudo tc qdisc add dev lo root netem delay 50ms loss 1%
- Servidor (Term. 1): ./servertcp 8080
- Proxy (Term. 2): ./proxytcp 9090 127.0.0.1 8080 (Sem Otimização)
- Cliente (Term. 3): cat trafego2.txt | ./clienttcp 127.0.0.1 9090 
- Repita todo o processo novamente, porém com o proxy: ./proxytcp 9090 127.0.0.1 8080 --otimizar
* Como o Proxy já gera o arquivo log.txt de forma automatica contendo todos os dados, poderá renomear o log.csv gerado para log_50ms_sem.csv e log_50ms_com.csv

Cenário 3: Latência 100ms com 2% Perda 
- Controle de Rede (Term. 4):
  sudo tc qdisc del dev lo root
  sudo tc qdisc add dev lo root netem delay 100ms loss 2%
- Servidor (Term. 1): ./servertcp 8080
- Proxy (Term. 2): ./proxytcp 9090 127.0.0.1 8080 (Sem Otimização)
- Cliente (Term. 3): cat trafego2.txt | ./clienttcp 127.0.0.1 9090 
- Repita todo o processo novamente, porém com o proxy: ./proxytcp 9090 127.0.0.1 8080 --otimizar
* Como o Proxy já gera o arquivo log.txt de forma automatica contendo todos os dados, poderá renomear o log.csv gerado para log_100ms_sem.csv e log_100ms_com.csv

Cenário 4: Limitação de Banda (5 Mbps)
- Controle de Rede (Term. 4):
  sudo tc qdisc del dev lo root
  sudo tc qdisc add dev lo root tbf rate 5mbit burst 32kbit latency 400ms
- Servidor (Term. 1): ./servertcp 8080
- Proxy (Term. 2): ./proxytcp 9090 127.0.0.1 8080 (Sem Otimização)
- Cliente (Term. 3): cat trafego.txt | ./clienttcp 127.0.0.1 9090 
- Repita todo o processo novamente, porém com o proxy: ./proxytcp 9090 127.0.0.1 8080 --otimizar
* Como o Proxy já gera o arquivo log.txt de forma automatica contendo todos os dados, poderá renomear o log.csv gerado para log_5mbps_sem.csv e log_5mbps_com.csv



Gerar gráficos:
- Após realizar todos os testes, deverá posuir 8 arquivos .csv na mesma pasta dos arquivos
- Execute este código:
  python3 gerar_7_graficos.py
- Os gráficos serão gerados automaticamente na pasta graficos_finais/



Limpeza
- Remova as limitações de rede para restaurar o funcionamento normal do seu computador:
  sudo tc qdisc del dev lo root