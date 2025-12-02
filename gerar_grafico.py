import pandas as pd
import matplotlib.pyplot as plt
import os

#Arquivos
cenarios = [
    ("log_base_sem.csv", "log_base_com.csv", "Cenario_1_Base"),
    ("log_50ms_sem.csv", "log_50ms_com.csv", "Cenario_2_50ms"),
    ("log_100ms_sem.csv", "log_100ms_com.csv", "Cenario_3_100ms"),
    ("log_5mbps_sem.csv", "log_5mbps_com.csv", "Cenario_4_5Mbps")
]

#Lista das 7 métricas
metricas_para_plotar = [
    ('RTT_ms', 'RTT Estimado', '1_rtt', '-'),
    ('RTTvar_ms', 'Variação (RTTVAR)', '2_rttvar', ':'),
    ('Retransmissions', 'Taxa de Retransmissões', '3_retransmissoes', '-'),
    ('CWND', 'Janela de Congestionamento (CWND)', '4_cwnd', '-'),
    ('SSTHRESH', 'Limiar ssthresh', '5_ssthresh', '--'),
    ('Throughput_Mbps', 'Throughput', '6_throughput', '-'),
    ('Throughput_Mbps', 'Goodput (Dados Úteis)', '7_goodput', '-') #Mesmo dado, visualização separada
]

#Leitura do CSV
def ler_csv(arquivo):
    try:
        df = pd.read_csv(arquivo)
        cols_num = ['Throughput_Mbps', 'RTT_ms', 'RTTvar_ms', 'CWND', 'SSTHRESH', 'Retransmissions', 'Timestamp']
        for col in cols_num:
            df[col] = pd.to_numeric(df[col], errors='coerce')
        return df
    except:
        return pd.DataFrame()

def criar_pasta():
    if not os.path.exists('graficos_finais'):
        os.makedirs('graficos_finais')

def gerar_grafico_individual(df_sem, df_com, col_dados, titulo, nome_saida, cenario_nome):
    plt.figure(figsize=(10, 6))
    
    #Filtra dados válidos
    sem = df_sem.dropna(subset=[col_dados])
    com = df_com.dropna(subset=[col_dados])

    #Plotagem
    plt.plot(sem['Timestamp'], sem[col_dados], color='red', linestyle='--', label='Sem Otimização', alpha=0.7)
    plt.plot(com['Timestamp'], com[col_dados], color='blue', linestyle='-', label='Com Otimização', linewidth=1.5)

    #Estilização
    plt.title(f'{titulo} - {cenario_nome.replace("_", " ")}')
    plt.xlabel('Tempo (s)')
    plt.ylabel('Valor')
    plt.legend()
    plt.grid(True, linestyle=':', alpha=0.6)
    
    #Salva
    caminho = f"graficos_finais/{cenario_nome}_{nome_saida}.png"
    plt.savefig(caminho, dpi=150)
    plt.close()
    print(f"Gerado: {caminho}")

#Execução principal
print("Iniciando geração de 7 gráficos por cenário...")
criar_pasta()

for arq_sem, arq_com, nome_cenario in cenarios:
    if not os.path.exists(arq_sem) or not os.path.exists(arq_com):
        print(f"Arquivos não encontrados para {nome_cenario}. Pulando...")
        continue

    df_sem = ler_csv(arq_sem)
    df_com = ler_csv(arq_com)

    #Loop para gerar os 7 gráficos deste cenário
    for coluna, titulo_grafico, sufixo_arq, estilo in metricas_para_plotar:
        gerar_grafico_individual(df_sem, df_com, coluna, titulo_grafico, sufixo_arq, nome_cenario)

print("\nConcluído! Verifique a pasta 'graficos_finais'.")
