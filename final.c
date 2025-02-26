#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "ssd1306.h"
#include "font.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "tusb.h"
#include "ws2812b.pio.h"

// Definições dos pinos
#define LED_COUNT 25  // 5x5 matriz de LEDs
#define LED_PIN 7
#define BOTAO_A 5
#define BOTAO_B 6
#define BUZZER_PIN 10  // Pino do buzzer
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C

// Variáveis globais para o relógio e cronômetro
static volatile bool cronometro_rodando = false;
static volatile bool cronometro_pausado = false;
static volatile uint32_t tempo_inicial_cronometro = 0;
static volatile uint32_t tempo_pausado_cronometro = 0;

// Variáveis para o relógio
static uint32_t relogio_segundos = 0; // Tempo inicial do relógio (ajuste conforme necessário)
static uint32_t ultima_atualizacao_relogio = 0;
static bool buzzer_ativado = false; // Controla o estado do buzzer

// Variáveis para a matriz de LEDs
static PIO pio;
static uint sm;

// Estrutura para armazenar dados dos LEDs
typedef struct {
    uint8_t R;
    uint8_t G;
    uint8_t B;
} led;

volatile led matriz_led[LED_COUNT] = {0};

// Mapeamento dos números na matriz 5x5 (1 a 12)
const uint8_t leds_por_numero[12][13] = {
    {22, 16, 17, 12, 7, 3, 2, 1},  // 1
    {23, 22, 21, 18, 13, 12, 11, 6, 3, 2, 1},  // 2
    {23, 22, 21, 18, 13, 12, 11, 8, 3, 2, 1},  // 3
    {21, 17, 18, 13, 11, 5, 6, 7, 8, 9, 1},  // 4
    {23, 22, 21, 16, 13, 12, 11, 8, 1, 2, 3},  // 5
    {23, 22, 21, 16, 13, 6, 3, 2, 1, 8, 11, 12, 1},  // 6
    {24, 23, 22, 21, 20, 18, 12, 6, 4},  // 7
    {23, 22, 21, 18, 16, 13, 12, 11, 6, 8, 3, 2, 1},  // 8
    {23, 22, 21, 16, 18, 13, 12, 11, 8, 3, 2, 1},  // 9
    {23, 15, 16, 13, 6, 12, 18, 10, 8},  // 10
    {23, 15, 16, 13, 6, 20, 18, 19, 10, 9},  // 11
    {23, 15, 16, 13, 6, 21, 20, 19, 10, 11, 8, 1, 0}   // 12
};

// Função para converter valores RGB em formato adequado
uint32_t valor_rgb(uint8_t B, uint8_t R, uint8_t G) {
    return (G << 24) | (R << 16) | (B << 8);
}

// Função que atualiza o estado dos LEDs na matriz
void set_led(uint8_t indice, uint8_t r, uint8_t g, uint8_t b) {
    if (indice < LED_COUNT) {
        matriz_led[indice].R = r;
        matriz_led[indice].G = g;
        matriz_led[indice].B = b;
    }
}

// Função que limpa a matriz de LEDs
void clear_leds() {
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        matriz_led[i].R = 0;
        matriz_led[i].B = 0;
        matriz_led[i].G = 0;
    }
}

// Função que exibe os LEDs da matriz
void print_leds() {
    uint32_t valor;
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        valor = valor_rgb(matriz_led[i].B, matriz_led[i].R, matriz_led[i].G);
        pio_sm_put_blocking(pio, sm, valor);
    }

    // Enviar um LED extra apagado para garantir atualização correta
    pio_sm_put_blocking(pio, sm, valor_rgb(0, 0, 0));
}

// Função para emitir um "bip" com o buzzer
void emitir_bip() {
    for (int i = 0; i < 5; i++) { // Repete 5 vezes para criar um "bip"
        gpio_put(BUZZER_PIN, 1); // Liga o buzzer
        sleep_ms(100);           // Mantém ligado por 100 ms
        gpio_put(BUZZER_PIN, 0); // Desliga o buzzer
        sleep_ms(100);           // Mantém desligado por 100 ms
    }
}

// Função para atualizar a matriz de LEDs com o número da hora
void atualizar_matriz(uint8_t numero, bool is_pm) {
    clear_leds();

    // Definição da cor (vermelho para AM, azul para PM)
    uint8_t R = is_pm ? 0 : 50;  // Vermelho para AM, 0 para PM
    uint8_t G = 0;                // Verde sempre 0
    uint8_t B = is_pm ? 50 : 0;   // Azul para PM, 0 para AM

    // Acende os LEDs corretos para o número atual
    for (uint8_t i = 0; i < 13; i++) {
        uint8_t indice = leds_por_numero[numero - 1][i]; // Subtrai 1 para ajustar ao índice do array

        // Verifica se o LED 0 deve ser ligado (apenas para o número 12)
        if (indice == 0 && numero != 12) {
            continue; // Ignora o LED 0 para todos os números, exceto 12
        }

        if (indice < LED_COUNT) {
            set_led(indice, R, G, B);
        }
    }

    // Atualiza a exibição
    print_leds();
}

// Função para atualizar o display OLED com o relógio e o cronômetro
void atualizar_display(ssd1306_t *ssd, uint32_t tempo_cronometro) {
    char buffer[20];

    // Calcula o tempo do relógio
    uint32_t horas_relogio = relogio_segundos / 3600;
    uint32_t minutos_relogio = (relogio_segundos % 3600) / 60;
    uint32_t segundos_relogio = relogio_segundos % 60;

    // Calcula o tempo do cronômetro
    uint32_t horas_cronometro = tempo_cronometro / 3600;
    uint32_t minutos_cronometro = (tempo_cronometro % 3600) / 60;
    uint32_t segundos_cronometro = tempo_cronometro % 60;

    // Limpa o display
    ssd1306_fill(ssd, false);

    // Exibe o relógio
    ssd1306_draw_string(ssd, "Relogio:", 0, 0);
    sprintf(buffer, "%02lu:%02lu:%02lu", horas_relogio, minutos_relogio, segundos_relogio);
    ssd1306_draw_string(ssd, buffer, 0, 10);

    // Exibe o cronômetro
    ssd1306_draw_string(ssd, "Cronometro:", 0, 20);
    sprintf(buffer, "%02lu:%02lu:%02lu", horas_cronometro, minutos_cronometro, segundos_cronometro);
    ssd1306_draw_string(ssd, buffer, 0, 30);

    // Envia os dados para o display
    ssd1306_send_data(ssd);
}

// Função de callback para os botões
void gpio_callback(uint gpio, uint32_t events) {
    static uint32_t tempo_antes = 0;
    uint32_t tempo_agora = to_ms_since_boot(get_absolute_time());

    // Debounce
    if (tempo_agora - tempo_antes > 200) {
        if (gpio == BOTAO_A) {
            if (!cronometro_rodando) {
                // Inicia o cronômetro
                cronometro_rodando = true;
                cronometro_pausado = false;
                tempo_inicial_cronometro = tempo_agora / 1000;
            } else {
                // Reinicia o cronômetro
                cronometro_rodando = false;
                cronometro_pausado = false;
                tempo_inicial_cronometro = 0;
                tempo_pausado_cronometro = 0;
            }
        } else if (gpio == BOTAO_B) {
            if (cronometro_rodando) {
                if (cronometro_pausado) {
                    // Despausa o cronômetro
                    cronometro_pausado = false;
                    tempo_inicial_cronometro += (tempo_agora / 1000) - tempo_pausado_cronometro;
                } else {
                    // Pausa o cronômetro
                    cronometro_pausado = true;
                    tempo_pausado_cronometro = tempo_agora / 1000;
                }
            }
        }
        tempo_antes = tempo_agora;
    }
    gpio_acknowledge_irq(gpio, events);
}

int main() {
    // Inicializa as comunicações seriais
    stdio_init_all();

    // Configurações das GPIO para os botões
    gpio_init(BOTAO_A);
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_A);
    gpio_pull_up(BOTAO_B);

    // Configurações das interrupções dos botões
    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled(BOTAO_B, GPIO_IRQ_EDGE_FALL, true);

    // Configuração do buzzer
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0); // Garante que o buzzer comece desligado

    // Configuração da matriz de LEDs
    pio = pio0;
    set_sys_clock_khz(128000, false);
    uint offset = pio_add_program(pio, &ws2812b_program);
    sm = pio_claim_unused_sm(pio, true);
    ws2812b_program_init(pio, sm, offset, LED_PIN);

    // Configurações para o display OLED
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_t ssd;
    ssd1306_init(&ssd, 128, 64, false, endereco, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    // Define o tempo inicial do relógio (12:59:50)
    relogio_segundos = (11 * 3600) + (59 * 60) + 45; // 12 horas, 59 minutos e 50 segundos
    ultima_atualizacao_relogio = to_ms_since_boot(get_absolute_time());

    // Loop principal
    while (true) {
        uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());

        // Atualiza o relógio a cada segundo
        if (tempo_atual - ultima_atualizacao_relogio >= 1000) {
            relogio_segundos++;
            ultima_atualizacao_relogio = tempo_atual;

            // Verifica se é uma hora cheia (minutos e segundos = 0)
            uint32_t minutos = (relogio_segundos % 3600) / 60;
            uint32_t segundos = relogio_segundos % 60;
            if (minutos == 0 && segundos == 0) {
                // Emite um "bip" com o buzzer
                emitir_bip();
            }

            // Calcula a hora atual (1 a 12) e verifica se é AM ou PM
            uint8_t hora = (relogio_segundos / 3600) % 12;
            if (hora == 0) hora = 12; // Ajusta 0 para 12
            bool is_pm = (relogio_segundos / 3600) >= 12;

            // Atualiza a matriz de LEDs com a hora atual
            atualizar_matriz(hora, is_pm);

            // Atualiza o display OLED
            uint32_t tempo_cronometro = 0;
            if (cronometro_rodando) {
                if (cronometro_pausado) {
                    tempo_cronometro = tempo_pausado_cronometro - tempo_inicial_cronometro;
                } else {
                    tempo_cronometro = (tempo_atual / 1000) - tempo_inicial_cronometro;
                }
            }
            atualizar_display(&ssd, tempo_cronometro);
        }

        // Espera um pouco antes de atualizar novamente
        sleep_ms(100);
    }

    return 0;
}