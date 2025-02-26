#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "ssd1306.h"
#include "font.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "tusb.h"

#define BOTAO_A 5
#define BOTAO_B 6
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

// Função para atualizar o display com o relógio e o cronômetro
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

    // Define o tempo inicial do relógio (ajuste conforme necessário)
    // Exemplo: 12 horas, 34 minutos e 56 segundos
    relogio_segundos = (12 * 3600) + (34 * 60) + 56;
    ultima_atualizacao_relogio = to_ms_since_boot(get_absolute_time());

    // Loop principal
    while (true) {
        uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());

        // Atualiza o relógio a cada segundo
        if (tempo_atual - ultima_atualizacao_relogio >= 1000) {
            relogio_segundos++;
            ultima_atualizacao_relogio = tempo_atual;
        }

        // Calcula o tempo decorrido do cronômetro
        uint32_t tempo_cronometro = 0;
        if (cronometro_rodando) {
            if (cronometro_pausado) {
                tempo_cronometro = tempo_pausado_cronometro - tempo_inicial_cronometro;
            } else {
                tempo_cronometro = (tempo_atual / 1000) - tempo_inicial_cronometro;
            }
        }

        // Atualiza o display com o tempo do relógio e do cronômetro
        atualizar_display(&ssd, tempo_cronometro);

        // Espera um pouco antes de atualizar novamente
        sleep_ms(100);
    }

    return 0;
}