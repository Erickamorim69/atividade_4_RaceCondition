#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

/* Registrar o módulo de log */
LOG_MODULE_REGISTER(race_condition_demo, LOG_LEVEL_INF);

/* Configurações dos testes */
#define STACK_SIZE             1024
#define THREAD_PRIORITY        5
#define INCREMENTS_PER_THREAD  50000
#define TOTAL_THREADS          2
#define EXPECTED_FINAL_VALUE   (INCREMENTS_PER_THREAD * TOTAL_THREADS)

/* Mapeamento dos LEDs RGB nativos da placa FRDM-KL25Z via Devicetree */
#define RED_LED_NODE   DT_ALIAS(led2)
#define GREEN_LED_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led2   = GPIO_DT_SPEC_GET_OR(RED_LED_NODE, gpios, {0});
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET_OR(GREEN_LED_NODE, gpios, {0});

/* 
 * VARIÁVEL COMPARTILHADA:
 * Declarada como volatile para evitar otimizações de registrador pelo compilador.
 * SEM MÚTEX, SEMÁFORO OU OPERAÇÕES ATÔMICAS -> Suscetível a Race Condition.
 */
static volatile uint32_t shared_counter = 0;

/* Stacks e estruturas das Threads */
K_THREAD_STACK_DEFINE(stack_a, STACK_SIZE);
K_THREAD_STACK_DEFINE(stack_b, STACK_SIZE);
static struct k_thread thread_a_data;
static struct k_thread thread_b_data;

/*
 * Função executada por ambas as threads.
 * Força a Condição de Corrida desmembrando o incremento `shared_counter++`
 * em etapas não atômicas intercaladas por `k_yield()`.
 */
static void worker_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (int i = 0; i < INCREMENTS_PER_THREAD; i++) {
        /* ETAPA 1: Leitura do valor atual para uma variável local de registradores */
        uint32_t temp = shared_counter;

        /* 
         * ETAPA 2: Força a abdicação da CPU.
         * Como ambas as threads possuem a mesma prioridade (5), a thread B assume a CPU
         * e lê exatamente o mesmo valor de `shared_counter` antes que a thread A consiga escrever.
         */
        k_yield();

        /* ETAPA 3: Escrita não atômica do valor incrementado */
        shared_counter = temp + 1;
    }
}

int main(void)
{
    LOG_INF("==================================================");
    LOG_INF("   Zephyr RTOS - Demonstracao de Race Condition   ");
    LOG_INF("   Placa Target: NXP FRDM-KL25Z (Cortex-M0+)      ");
    LOG_INF("==================================================");

    /* Inicializa os pinos do LED RGB nativo */
    if (gpio_is_ready_dt(&led0)) {
        gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    }
    if (gpio_is_ready_dt(&led2)) {
        gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
    }

    LOG_INF("Valor inicial do contador : %u", shared_counter);
    LOG_INF("Valor final ESPERADO      : %u", EXPECTED_FINAL_VALUE);
    LOG_INF("Iniciando %d threads concorrentes (%d incrementos cada)...", 
            TOTAL_THREADS, INCREMENTS_PER_THREAD);

    /* Criar Thread A */
    k_tid_t tid_a = k_thread_create(&thread_a_data, stack_a,
                                    K_THREAD_STACK_SIZEOF(stack_a),
                                    worker_thread, NULL, NULL, NULL,
                                    THREAD_PRIORITY, 0, K_NO_WAIT);

    /* Criar Thread B com a mesma prioridade */
    k_tid_t tid_b = k_thread_create(&thread_b_data, stack_b,
                                    K_THREAD_STACK_SIZEOF(stack_b),
                                    worker_thread, NULL, NULL, NULL,
                                    THREAD_PRIORITY, 0, K_NO_WAIT);

    /* Aguarda o término de ambas as threads */
    k_thread_join(tid_a, K_FOREVER);
    k_thread_join(tid_b, K_FOREVER);

    /* Leitura e Diagnóstico Final */
    uint32_t final_value = shared_counter;

    LOG_INF("--------------------------------------------------");
    LOG_INF("--- Processamento das Threads Concluido ---");
    LOG_INF("Valor REAL obtido na memoria: %u", final_value);

    if (final_value < EXPECTED_FINAL_VALUE) {
        uint32_t lost_increments = EXPECTED_FINAL_VALUE - final_value;
        float percent_lost = ((float)lost_increments / EXPECTED_FINAL_VALUE) * 100.0f;

        LOG_ERR("FALHA DETECTADA! Condicao de Corrida confirmada!");
        LOG_ERR("Incrementos PERDIDOS: %u (%.2f%% de perda de dados)", 
                lost_increments, (double)percent_lost);

        /* Feedback Visual: Acende o LED Vermelho */
        if (gpio_is_ready_dt(&led2)) {
            gpio_pin_set_dt(&led2, 1);
        }
    } else {
        LOG_INF("SUCESSO: Nenhum incremento foi perdido.");

        /* Feedback Visual: Acende o LED Verde */
        if (gpio_is_ready_dt(&led0)) {
            gpio_pin_set_dt(&led0, 1);
        }
    }

    return 0;
}