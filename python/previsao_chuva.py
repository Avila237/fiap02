# ============================================================
# FarmTech Solutions — Previsão de Chuva via OpenWeather API
# FIAP — Fase 2 | Integração Python + ESP32 (Wokwi)
# ============================================================
# Consulta a previsão do tempo para Ijuí/RS e indica ao usuário
# qual comando digitar no Monitor Serial do Wokwi para controlar
# a bomba de irrigação com base na chuva prevista.
# ============================================================

import requests
import os
from datetime import datetime
from dotenv import load_dotenv

# ── Constantes de configuração ───────────────────────────────
CIDADE = "Ijui,BR"
HORAS_A_ANALISAR = 6

# Tipos de clima da API considerados como "chuva"
TIPOS_CHUVA = ["Rain", "Drizzle", "Thunderstorm"]


# ─────────────────────────────────────────────────────────────
# Carrega o arquivo .env e retorna a API key.
# Encerra o programa se a chave não for encontrada.
# ─────────────────────────────────────────────────────────────
def carregar_api_key():
    load_dotenv()
    chave = os.getenv("OPENWEATHER_API_KEY")

    if not chave:
        print("❌ API key não encontrada.")
        print("   Crie um arquivo .env com: OPENWEATHER_API_KEY=sua_chave")
        print("   Obtenha uma chave gratuita em: https://home.openweathermap.org/api_keys")
        exit(1)

    return chave


# ─────────────────────────────────────────────────────────────
# Consulta a API de previsão e retorna os dados em formato JSON.
# Parâmetro cidade: string no formato "Cidade,Pais" (ex: "Ijui,BR")
# ─────────────────────────────────────────────────────────────
def consultar_previsao(cidade, api_key):
    url = "https://api.openweathermap.org/data/2.5/forecast"

    parametros = {
        "q":     cidade,
        "appid": api_key,
        "units": "metric",   # temperaturas em graus Celsius
        "lang":  "pt_br",    # descrições em português
    }

    try:
        resposta = requests.get(url, params=parametros, timeout=10)

        # Verificar erros comuns antes de retornar os dados
        if resposta.status_code == 401:
            print("❌ API key inválida ou ainda não ativa (leva até 2h após cadastro).")
            exit(1)

        if resposta.status_code == 404:
            print(f"❌ Cidade não encontrada: {cidade}")
            exit(1)

        if resposta.status_code != 200:
            print(f"❌ Erro na API — status {resposta.status_code}: {resposta.text}")
            exit(1)

        return resposta.json()

    except requests.exceptions.Timeout:
        print("❌ Tempo limite excedido ao conectar com a API (timeout de 10s).")
        exit(1)

    except requests.exceptions.ConnectionError:
        print("❌ Sem conexão com a internet. Verifique sua rede.")
        exit(1)


# ─────────────────────────────────────────────────────────────
# Analisa os primeiros blocos da previsão (cada bloco = 3 horas).
# Para 6 horas, analisa 2 blocos. Retorna um dicionário com o
# resultado geral e os detalhes de cada período.
# ─────────────────────────────────────────────────────────────
def analisar_proximas_horas(dados_api, horas=6):
    # A API retorna blocos de 3 em 3 horas
    quantidade_blocos = horas // 3
    blocos = dados_api["list"][:quantidade_blocos]

    detalhes = []
    vai_chover = False

    for bloco in blocos:
        # Converter timestamp Unix para hora legível (ex: "14:00")
        timestamp = bloco["dt"]
        horario = datetime.fromtimestamp(timestamp).strftime("%H:%M")

        # Informações do clima do bloco
        clima_principal = bloco["weather"][0]["main"]         # ex: "Rain"
        descricao       = bloco["weather"][0]["description"]  # ex: "chuva leve"
        temperatura     = bloco["main"]["temp"]               # ex: 22.5

        # Verificar se este bloco tem chuva
        tem_chuva = clima_principal in TIPOS_CHUVA

        if tem_chuva:
            vai_chover = True  # basta um bloco com chuva para acionar o alerta

        detalhes.append({
            "horario":    horario,
            "descricao":  descricao,
            "temperatura": temperatura,
            "tem_chuva":  tem_chuva,
        })

    return {
        "vai_chover": vai_chover,
        "detalhes":   detalhes,
    }


# ─────────────────────────────────────────────────────────────
# Imprime o relatório formatado com a previsão e a instrução
# para o usuário digitar no Monitor Serial do Wokwi.
# ─────────────────────────────────────────────────────────────
def imprimir_relatorio(resultado, cidade):
    vai_chover = resultado["vai_chover"]
    detalhes   = resultado["detalhes"]

    # Nome da cidade legível para exibição
    nome_cidade = cidade.replace(",BR", "/RS")

    # Cabeçalho — emoji muda conforme previsão
    emoji_cabecalho = "🌦️" if vai_chover else "☀️"
    print()
    print("═══════════════════════════════════════════════════")
    print(f"{emoji_cabecalho}  PREVISÃO DO TEMPO — {nome_cidade}")
    print("═══════════════════════════════════════════════════")

    # Detalhe de cada bloco de 3 horas
    print(f"\nPróximas {HORAS_A_ANALISAR} horas:\n")
    for bloco in detalhes:
        emoji_bloco = "🌧️" if bloco["tem_chuva"] else "☀️"
        print(f"  [{bloco['horario']}] {bloco['descricao']} — {bloco['temperatura']:.1f}°C  {emoji_bloco}")

    # Resultado resumido
    print()
    print("───────────────────────────────────────────────────")
    if vai_chover:
        print("RESULTADO: CHUVA PREVISTA nas próximas 6 horas")
    else:
        print("RESULTADO: SEM CHUVA prevista")
    print("───────────────────────────────────────────────────")

    # Instrução para o Monitor Serial do Wokwi
    print()
    if vai_chover:
        print('➡️  Digite "1" no Monitor Serial do Wokwi')
        print("   (a bomba será DESATIVADA para economizar água)")
    else:
        print('➡️  Digite "0" no Monitor Serial do Wokwi')
        print("   (irrigação AUTORIZADA conforme lógica do ESP32)")

    print()
    print("═══════════════════════════════════════════════════")
    print()


# ─────────────────────────────────────────────────────────────
# Função principal — orquestra todas as etapas do programa
# ─────────────────────────────────────────────────────────────
def main():
    api_key   = carregar_api_key()
    dados_api = consultar_previsao(CIDADE, api_key)
    resultado = analisar_proximas_horas(dados_api, horas=HORAS_A_ANALISAR)
    imprimir_relatorio(resultado, CIDADE)


# Ponto de entrada do script
if __name__ == "__main__":
    main()
