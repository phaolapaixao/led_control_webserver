#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"

// Configurações de Wi-Fi
#define WIFI_SSID "ANA"
#define WIFI_PASSWORD "Phaola2023"

// Definição dos pinos
#define LED_PIN CYW43_WL_GPIO_LED_PIN
#define LED_ALARM_PIN 13   // LED vermelho para alarme
#define FAN_PIN1 12        // LED azul para ventilador
#define FAN_PIN2 11        // LED verde para ventilador (usaremos como roxo)
#define BUTTON_A_PIN 5   // Botão A
#define BUTTON_B_PIN 6   // Botão B

// Variáveis globais
static bool alarm_enabled = true;
static bool fan_enabled = false;
static bool button_a_state = false;
static bool button_b_state = false;
static absolute_time_t last_fan_toggle = 0;
static bool fan_toggle_state = false;

// Função para ler temperatura interna
float read_internal_temperature() {
    adc_select_input(4);
    uint16_t raw_value = adc_read();
    const float conversion_factor = 3.3f / (1 << 12);
    return 27.0f - ((raw_value * conversion_factor) - 0.706f) / 0.001721f;
}

// Função para controlar o ventilador
void control_fan() {
    if (fan_enabled) {
        if (absolute_time_diff_us(last_fan_toggle, get_absolute_time()) > 500000) { // 0.5s
            fan_toggle_state = !fan_toggle_state;
            last_fan_toggle = get_absolute_time();
            
            gpio_put(FAN_PIN1, fan_toggle_state);
            gpio_put(FAN_PIN2, !fan_toggle_state);
        }
    } else {
        gpio_put(FAN_PIN1, 0);
        gpio_put(FAN_PIN2, 0);
    }
}

// Função para verificar botões
void check_buttons() {
    button_a_state = !gpio_get(BUTTON_A_PIN); // Invertido porque normalmente pull-up
    button_b_state = !gpio_get(BUTTON_B_PIN);
}

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("Request: %s\n", request);

    // Processa comandos
    if (strstr(request, "GET /alarm_on") != NULL) {
        alarm_enabled = true;
        gpio_put(LED_ALARM_PIN, 1);
    } else if (strstr(request, "GET /alarm_off") != NULL) {
        alarm_enabled = false;
        gpio_put(LED_ALARM_PIN, 0);
    } else if (strstr(request, "GET /fan_on") != NULL) {
        fan_enabled = true;
    } else if (strstr(request, "GET /fan_off") != NULL) {
        fan_enabled = false;
    }

    // Atualiza estados
    check_buttons();
    float temperature = read_internal_temperature();
    
    // Verifica superaquecimento (limite de 50°C para exemplo)
    if (temperature > 50.0f && alarm_enabled) {
        gpio_put(LED_ALARM_PIN, 1);
    }

    // Cria a resposta HTML com atualização automática
    char html[2048];
    snprintf(html, sizeof(html),
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "\r\n"
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "<title>Monitor de Cabine de Carga</title>\n"
    "<meta http-equiv=\"refresh\" content=\"1\">\n"
    "</head>\n"
    "<body>\n"
    "<h1>Monitor de Cabine de Carga</h1>\n"
    "<div>\n"
    "<div>\n"
    "Temperatura: %.2f &deg;C\n"
    "</div>\n"
    "<div>\n"
    "<h2>Sistema de Alarme</h2>\n"
    "<div>Estado: %s</div>\n"
    "<a href=\"./alarm_on\"><button>Ligar</button></a>\n"
    "<a href=\"./alarm_off\"><button>Desligar</button></a>\n"
    "</div>\n"
    "<div>\n"
    "<h2>Ventilador</h2>\n"
    "<div>Estado: %s</div>\n"
    "<a href=\"./fan_on\"><button>Ligar</button></a>\n"
    "<a href=\"./fan_off\"><button>Desligar</button></a>\n"
    "</div>\n"
    "<div>\n"
    "<h2>Botões</h2>\n"
    "<div>Botão A: %s</div>\n"
    "<div>Botão B: %s</div>\n"
    "</div>\n"
    "</div>\n"
    "</body>\n"
    "</html>\n",
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
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

int main() {
    stdio_init_all();

    // Configuração dos pinos
    gpio_init(LED_ALARM_PIN);
    gpio_set_dir(LED_ALARM_PIN, GPIO_OUT);
    
    gpio_init(FAN_PIN1);
    gpio_set_dir(FAN_PIN1, GPIO_OUT);
    
    gpio_init(FAN_PIN2);
    gpio_set_dir(FAN_PIN2, GPIO_OUT);
    
    gpio_init(BUTTON_A_PIN);
    gpio_set_dir(BUTTON_A_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_A_PIN);
    
    gpio_init(BUTTON_B_PIN);
    gpio_set_dir(BUTTON_B_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_B_PIN);

    // Inicializa Wi-Fi
    while (cyw43_arch_init()) {
        printf("Falha ao inicializar Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    cyw43_arch_gpio_put(LED_PIN, 0);
    cyw43_arch_enable_sta_mode();

    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000)) {
        printf("Falha ao conectar ao Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    printf("Conectado ao Wi-Fi\n");

    if (netif_default) {
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Configura o servidor TCP
    struct tcp_pcb *server = tcp_new();
    if (!server) {
        printf("Falha ao criar servidor TCP\n");
        return -1;
    }

    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("Falha ao associar servidor TCP à porta 80\n");
        return -1;
    }

    server = tcp_listen(server);
    tcp_accept(server, tcp_server_accept);

    printf("Servidor ouvindo na porta 80\n");

    // Inicializa o ADC
    adc_init();
    adc_set_temp_sensor_enabled(true);

    while (true) {
        check_buttons();
        control_fan();
        
        // Verifica temperatura periodicamente
        float temp = read_internal_temperature();
        if (temp > 50.0f && alarm_enabled) { // Limite de 50°C
            gpio_put(LED_ALARM_PIN, 1);
        }
        
        cyw43_arch_poll();
        sleep_ms(10);
    }

    cyw43_arch_deinit();
    return 0;
}