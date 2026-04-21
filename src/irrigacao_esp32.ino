// ============================================================
// FarmTech Solutions — Sistema de Irrigação Inteligente
// FIAP — Fase 2 | Simulação no Wokwi com ESP32
// ============================================================

#include <DHT.h>

// ── Definições de pinos ──────────────────────────────────────
#define PINO_BOTAO_N   13   // Nutriente N (INPUT_PULLUP — LOW = pressionado)
#define PINO_BOTAO_P    4   // Nutriente P (INPUT_PULLUP — LOW = pressionado)
#define PINO_BOTAO_K    5   // Nutriente K (INPUT_PULLUP — LOW = pressionado)
#define PINO_LDR       34   // LDR representa o pH do solo (leitura analógica)
#define PINO_DHT       18   // DHT22 representa a umidade do solo
#define PINO_RELE      19   // Relé que controla a bomba d'água

#define DHTTYPE DHT22

// ── Objeto do sensor DHT ─────────────────────────────────────
DHT dht(PINO_DHT, DHTTYPE);

// ── Variáveis globais de estado ──────────────────────────────
bool nutriente_N       = false;  // Nitrogênio presente no solo
bool nutriente_P       = false;  // Fósforo presente no solo
bool nutriente_K       = false;  // Potássio presente no solo
float ph_solo          = 7.0;    // pH calculado a partir do LDR
float umidade_solo     = 0.0;    // Umidade lida pelo DHT22
bool bomba_ligada      = false;  // Estado atual da bomba
bool chuva_prevista    = false;  // Atualizado via Serial (API de clima)
bool umidade_override_ativo = false;  // true quando umidade foi injetada via Serial
bool ph_override_ativo      = false;  // true quando pH foi injetado via Serial

// ─────────────────────────────────────────────────────────────
// Lê os três botões e os dois sensores; atualiza as variáveis globais
// ─────────────────────────────────────────────────────────────
void ler_sensores() {
  // Botões — sempre lê
  int leitura_N = digitalRead(PINO_BOTAO_N);
  int leitura_P = digitalRead(PINO_BOTAO_P);
  int leitura_K = digitalRead(PINO_BOTAO_K);

  Serial.print(">>> DEBUG Botões | raw N=");
  Serial.print(leitura_N);
  Serial.print(" P=");
  Serial.print(leitura_P);
  Serial.print(" K=");
  Serial.println(leitura_K);

  nutriente_N = (leitura_N == LOW);
  nutriente_P = (leitura_P == LOW);
  nutriente_K = (leitura_K == LOW);

  // pH — só lê do LDR se não houver override
  if (!ph_override_ativo) {
    int valor_ldr = analogRead(PINO_LDR);
    ph_solo = (float)map(valor_ldr, 0, 4095, 0, 14);
  }

  // Umidade — só lê do DHT se não houver override
  if (!umidade_override_ativo) {
    float leitura = dht.readHumidity();
    if (!isnan(leitura)) {
      umidade_solo = leitura;
    }
  }
}

// ─────────────────────────────────────────────────────────────
// Verifica mensagens chegando pela porta Serial
// '1'/'0' → chuva | 'u<val>' → umidade manual | 'p<val>' → pH manual
// ─────────────────────────────────────────────────────────────
void processar_serial() {
  if (!Serial.available()) return;

  String comando = Serial.readStringUntil('\n');
  comando.trim();

  if (comando.length() == 0) return;

  if (comando == "1") {
    chuva_prevista = true;
    Serial.println(">>> Chuva prevista: ATIVADA");
    return;
  }
  if (comando == "0") {
    chuva_prevista = false;
    Serial.println(">>> Chuva prevista: DESATIVADA");
    return;
  }

  if (comando.startsWith("u") || comando.startsWith("U")) {
    float valor = comando.substring(1).toFloat();
    if (valor >= 0 && valor <= 100) {
      umidade_solo = valor;
      umidade_override_ativo = true;
      Serial.print(">>> Umidade definida manualmente: ");
      Serial.print(valor);
      Serial.println("%");
    } else {
      Serial.println(">>> ERRO: umidade deve ser entre 0 e 100");
    }
    return;
  }

  if (comando.startsWith("p") || comando.startsWith("P")) {
    float valor = comando.substring(1).toFloat();
    if (valor >= 0 && valor <= 14) {
      ph_solo = valor;
      ph_override_ativo = true;
      Serial.print(">>> pH definido manualmente: ");
      Serial.println(valor);
    } else {
      Serial.println(">>> ERRO: pH deve ser entre 0 e 14");
    }
    return;
  }

  Serial.print(">>> Comando desconhecido: ");
  Serial.println(comando);
}

// ─────────────────────────────────────────────────────────────
// Aplica as regras de irrigação e aciona o relé
// Retorna a string com o motivo da decisão para o relatório
// ─────────────────────────────────────────────────────────────
String decidir_bomba() {
  String motivo = "";

  // Regra 1 — chuva prevista: economizar água
  if (chuva_prevista) {
    bomba_ligada = false;
    motivo = "chuva prevista — irrigação desnecessária";

  // Regra 2 — pH fora da faixa ideal para soja (5.5 a 7.0)
  } else if (ph_solo < 5.5 || ph_solo > 7.0) {
    bomba_ligada = false;
    motivo = "ALERTA: pH fora da faixa ideal para soja — corrigir solo";
    Serial.println("!! ALERTA: pH fora da faixa ideal para soja — corrigir solo !!");

  // Regra 3 — solo já úmido o suficiente
  } else if (umidade_solo >= 70.0) {
    bomba_ligada = false;
    motivo = "solo já úmido (>= 70%)";

  // Regra 4 — solo seco e ao menos um nutriente P ou K presente
  } else if (umidade_solo < 60.0 && (nutriente_P || nutriente_K)) {
    bomba_ligada = true;
    motivo = "solo seco (< 60%) e nutrientes presentes";

  // Regra 5a — solo seco (<60%) mas sem nutrientes P ou K disponíveis
  } else if (umidade_solo < 60.0 && !nutriente_P && !nutriente_K) {
    bomba_ligada = false;
    motivo = "solo seco, mas sem nutrientes P ou K presentes";

  // Regra 5b — zona intermediária (60-70%): mantém estado anterior (histerese)
  } else {
    motivo = "umidade intermediária (60-70%) — mantendo estado anterior";
    // bomba_ligada não é alterada (histerese simples)
  }

  // Aciona o pino do relé conforme a decisão
  digitalWrite(PINO_RELE, bomba_ligada ? HIGH : LOW);

  return motivo;
}

// ─────────────────────────────────────────────────────────────
// Imprime o relatório formatado no Monitor Serial
// ─────────────────────────────────────────────────────────────
void imprimir_estado(String motivo) {
  unsigned long segundos = millis() / 1000;

  Serial.println("==========================================");
  Serial.print("  Leitura [");
  Serial.print(segundos);
  Serial.println("s]");
  Serial.println("==========================================");

  // Nutrientes
  Serial.print("  N: ");
  Serial.print(nutriente_N ? "SIM" : "NAO");
  Serial.print(" | P: ");
  Serial.print(nutriente_P ? "SIM" : "NAO");
  Serial.print(" | K: ");
  Serial.println(nutriente_K ? "SIM" : "NAO");

  // pH
  Serial.print("  pH: ");
  Serial.println(ph_solo, 2);

  // Umidade
  Serial.print("  Umidade: ");
  Serial.print(umidade_solo, 1);
  Serial.println("%");

  // Chuva prevista
  Serial.print("  Chuva prevista (API): ");
  Serial.println(chuva_prevista ? "SIM" : "NAO");

  // Decisão da bomba
  Serial.print("  -> BOMBA: ");
  Serial.print(bomba_ligada ? "LIGADA" : "DESLIGADA");
  Serial.print("  -- motivo: ");
  Serial.println(motivo);

  Serial.println("==========================================");
  Serial.println();
}

// ─────────────────────────────────────────────────────────────
// Setup: configura pinos e inicia comunicações
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Botões com pull-up interno
  pinMode(PINO_BOTAO_N, INPUT_PULLUP);
  pinMode(PINO_BOTAO_P, INPUT_PULLUP);
  pinMode(PINO_BOTAO_K, INPUT_PULLUP);

  // Relé começa desligado
  pinMode(PINO_RELE, OUTPUT);
  digitalWrite(PINO_RELE, LOW);

  pinMode(PINO_DHT, INPUT_PULLUP);
  dht.begin();
  Serial.println("DHT22 inicializado no pino " + String(PINO_DHT));

  Serial.println("FarmTech Solutions — Sistema de Irrigacao Iniciado");
  Serial.println("Comandos disponiveis no Serial Monitor:");
  Serial.println("  1       -> ativar chuva prevista");
  Serial.println("  0       -> desativar chuva prevista");
  Serial.println("  u<val>  -> definir umidade manual (ex: u40)");
  Serial.println("  p<val>  -> definir pH manual (ex: p6.5)");
  Serial.println();
}

// ─────────────────────────────────────────────────────────────
// Loop principal: executa a cada 2 segundos
// ─────────────────────────────────────────────────────────────
void loop() {
  processar_serial();
  ler_sensores();
  String motivo = decidir_bomba();
  imprimir_estado(motivo);

  delay(2000); // DHT22 exige pelo menos 2 s entre leituras
}
