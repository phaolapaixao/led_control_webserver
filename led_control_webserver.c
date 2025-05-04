#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include <math.h>

// Configurações de Wi-Fi
#define WIFI_SSID "ANA"
#define WIFI_PASSWORD "Phaola2023"

// Definição dos pinos
#define LED_PIN CYW43_WL_GPIO_LED_PIN
#define LED_ALARM_PIN 13
#define FAN_PIN1 12
#define FAN_PIN2 11
#define FAN_PIN3 10
#define FAN_PIN4 9
#define BUTTON_A_PIN 5
#define BUTTON_B_PIN 6

// Declarações antecipadas das funções
float read_internal_temperature();
void control_fan();
void check_buttons();
void log_changes();

// Variáveis globais
static bool alarm_enabled = false;
static bool fan_enabled = false;
static bool button_a_state = false;
static bool button_b_state = false;
static absolute_time_t last_fan_toggle = 0;
static uint8_t fan_toggle_state = 0;

// Variáveis para monitorar mudanças
static float last_temperature = 0;
static bool last_alarm_state = false;
static bool last_fan_state = false;
static bool last_button_a = false;
static bool last_button_b = false;

// Função para ler temperatura interna
float read_internal_temperature() {
    adc_select_input(4);
    uint16_t raw_value = adc_read();
    const float conversion_factor = 3.3f / (1 << 12);
    return 27.0f - ((raw_value * conversion_factor) - 0.706f) / 0.001721f;
}

// Função para controlar a hélice
void control_fan() {
    if (fan_enabled) {
        if (absolute_time_diff_us(last_fan_toggle, get_absolute_time()) > 200000) {
            fan_toggle_state = (fan_toggle_state + 1) % 4;
            last_fan_toggle = get_absolute_time();

            // Apaga todos
            gpio_put(FAN_PIN1, 0);
            gpio_put(FAN_PIN2, 0);
            gpio_put(FAN_PIN3, 0);
            gpio_put(FAN_PIN4, 0);

            // Acende um por vez para simular rotação
            switch (fan_toggle_state) {
                case 0: gpio_put(FAN_PIN1, 1); break;
                case 1: gpio_put(FAN_PIN2, 1); break;
                case 2: gpio_put(FAN_PIN3, 1); break;
                case 3: gpio_put(FAN_PIN4, 1); break;
            }
        }
    } else {
        // Ventilador desligado, apaga todos
        gpio_put(FAN_PIN1, 0);
        gpio_put(FAN_PIN2, 0);
        gpio_put(FAN_PIN3, 0);
        gpio_put(FAN_PIN4, 0);
    }
}

// Função para verificar botões
void check_buttons() {
    button_a_state = !gpio_get(BUTTON_A_PIN);
    button_b_state = !gpio_get(BUTTON_B_PIN);
}

// Função para log de mudanças
void log_changes() {
    float current_temp = read_internal_temperature();

    if (alarm_enabled != last_alarm_state) {
        printf("[ALARME] Estado alterado: %s\n", alarm_enabled ? "LIGADO" : "DESLIGADO");
        last_alarm_state = alarm_enabled;
    }

    if (fan_enabled != last_fan_state) {
        printf("[VENTILADOR] Estado alterado: %s\n", fan_enabled ? "LIGADO" : "DESLIGADO");
        last_fan_state = fan_enabled;
    }

    check_buttons(); // Atualiza os estados dos botões
    if (button_a_state != last_button_a) {
        printf("[BOTÃO A] Estado alterado: %s\n", button_a_state ? "PRESSIONADO" : "LIVRE");
        last_button_a = button_a_state;
    }

    if (button_b_state != last_button_b) {
        printf("[BOTÃO B] Estado alterado: %s\n", button_b_state ? "PRESSIONADO" : "LIVRE");
        last_button_b = button_b_state;
    }
}

// Callback de recebimento TCP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("[HTTP] Requisição recebida: %s\n", request);

    if (strstr(request, "GET /alarm_on") != NULL) {
        printf("[HTTP] Comando recebido: Ligar alarme\n");
        alarm_enabled = true;
        gpio_put(LED_ALARM_PIN, 1);
    } else if (strstr(request, "GET /alarm_off") != NULL) {
        printf("[HTTP] Comando recebido: Desligar alarme\n");
        alarm_enabled = false;
        gpio_put(LED_ALARM_PIN, 0);
    } else if (strstr(request, "GET /fan_on") != NULL) {
        printf("[HTTP] Comando recebido: Ligar ventilador\n");
        fan_enabled = true;
    } else if (strstr(request, "GET /fan_off") != NULL) {
        printf("[HTTP] Comando recebido: Desligar ventilador\n");
        fan_enabled = false;
    }

    log_changes();
    float temperature = read_internal_temperature();

    if (temperature > 50.0f && alarm_enabled) {
        gpio_put(LED_ALARM_PIN, 1);
        printf("[ALARME] Temperatura crítica detectada: %.2f°C\n", temperature);
    }

    char html[2048];
    snprintf(html, sizeof(html),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<!DOCTYPE html><html><head><title>Monitor</title>"
        "<meta http-equiv=\"refresh\" content=\"1\"></head><body>"
        "<h1>Monitor de Cabine de Carga</h1>"
        "<div>Temperatura: %.2f &deg;C</div>"
        "<h2>Sistema de Alarme</h2>"
        "<div>Estado: %s</div>"
        "<a href=\"/alarm_on\"><button>Ligar</button></a>"
        "<a href=\"/alarm_off\"><button>Desligar</button></a>"
        "<h2>Ventilador</h2>"
        "<div>Estado: %s</div>"
        "<a href=\"/fan_on\"><button>Ligar</button></a>"
        "<a href=\"/fan_off\"><button>Desligar</button></a>"
        "<h2>Botoes</h2>"
        "<div>Botao A: %s</div>"
        "<div>Botao B: %s</div>"
        "</body></html>",
        temperature,
        alarm_enabled ? "LIGADO" : "DESLIGADO",
        fan_enabled ? "LIGADO" : "DESLIGADO",
        button_a_state ? "PRESSIONADO" : "LIVRE",
        button_b_state ? "PRESSIONADO" : "LIVRE");

    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    free(request);
    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    printf("[CONEXÃO] Novo cliente conectado\n");
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

int main() {
    stdio_init_all();

    // Inicializa pinos
    gpio_init(LED_ALARM_PIN); gpio_set_dir(LED_ALARM_PIN, GPIO_OUT);
    gpio_init(FAN_PIN1); gpio_set_dir(FAN_PIN1, GPIO_OUT);
    gpio_init(FAN_PIN2); gpio_set_dir(FAN_PIN2, GPIO_OUT);
    gpio_init(FAN_PIN3); gpio_set_dir(FAN_PIN3, GPIO_OUT);
    gpio_init(FAN_PIN4); gpio_set_dir(FAN_PIN4, GPIO_OUT);
    gpio_init(BUTTON_A_PIN); gpio_set_dir(BUTTON_A_PIN, GPIO_IN); gpio_pull_up(BUTTON_A_PIN);
    gpio_init(BUTTON_B_PIN); gpio_set_dir(BUTTON_B_PIN, GPIO_IN); gpio_pull_up(BUTTON_B_PIN);

    // Inicializa Wi-Fi
    if (cyw43_arch_init()) {
        printf("[ERRO] Falha ao inicializar Wi-Fi\n");
        return -1;
    }

    cyw43_arch_gpio_put(LED_PIN, 0);
    cyw43_arch_enable_sta_mode();

    printf("[Wi-Fi] Conectando ao Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000)) {
        printf("[ERRO] Falha ao conectar ao Wi-Fi\n");
        return -1;
    }

    printf("[Wi-Fi] Conectado ao Wi-Fi\n");

    if (netif_default) {
        printf("[REDE] IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Servidor TCP
    struct tcp_pcb *server = tcp_new();
    if (!server) {
        printf("[ERRO] Falha ao criar servidor TCP\n");
        return -1;
    }

    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("[ERRO] Falha ao associar porta 80\n");
        return -1;
    }

    server = tcp_listen(server);
    tcp_accept(server, tcp_server_accept);

    // Inicializa o ADC
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);

    printf("[SERVIDOR] Servidor HTTP rodando na porta 80\n");

    // Inicializa variáveis de estado
    last_temperature = read_internal_temperature();
    last_alarm_state = alarm_enabled;
    last_fan_state = fan_enabled;
    check_buttons();
    last_button_a = button_a_state;
    last_button_b = button_b_state;

    // Loop principal
    while (true) {
        log_changes();
        control_fan();

        float temp = read_internal_temperature();
        if (temp > 50.0f && alarm_enabled) {
            gpio_put(LED_ALARM_PIN, 1);
            printf("[ALERTA] Temperatura crítica: %.2f°C\n", temp);
        }

        cyw43_arch_poll();
        sleep_ms(100); // Reduzi o polling para 100ms
    }

    cyw43_arch_deinit();
    return 0;
}