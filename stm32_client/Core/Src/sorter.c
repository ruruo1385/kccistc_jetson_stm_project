#include "sorter.h"
#include "tracking.h"
#include "servo_motor.h"
#include "led.h"
#include "conveyor.h"
#include "main.h"
#include "communication.h"
#include "wifi.h"

volatile uint32_t sorter_results_accepted = 0U;
volatile uint32_t sorter_results_rejected = 0U;
volatile uint32_t sorter_results_consumed = 0U;
volatile uint8_t error_front = 0U;
volatile uint8_t error_left = 0U;
volatile uint8_t error_right = 0U;
volatile ProductResult error_result = PRODUCT_UNKNOWN;

#define QUEUE_SIZE       8U

#define NORMAL_US        1500U // PASS: straight-through home position
#define DEFECT_US        1200U // FAIL: 300us offset from home
#define HOME_US          1500U

#define TIMEOUT_MS       15000U
#define EXIT_STUCK_MS    5000U
#define CLEARANCE_MS     500U
#define FRONT_FILTER_MS  50U

typedef enum
{
    WAIT_FRONT,
    WAIT_EXIT_ON,
    WAIT_EXIT_OFF,
    CLEARANCE,
    STOPPED,
    ERROR_STATE
} SorterState;

static SorterState state;
static SorterError last_error;
static uint32_t started_at;

static ProductResult current_result;
static ProductResult waiting_result;
static uint8_t waiting_product;
static uint8_t front_previous;
static uint8_t front_candidate;
static uint8_t front_stable;
static uint32_t front_changed_at;

static ProductResult queue[QUEUE_SIZE];
static uint8_t queue_read;
static uint8_t queue_write;
static uint8_t queue_count;

static uint32_t normal_count;
static uint32_t defect_count;

static void ClearProducts(void)
{
    current_result = PRODUCT_UNKNOWN;
    waiting_result = PRODUCT_UNKNOWN;
    waiting_product = 0U;

    queue_read = 0U;
    queue_write = 0U;
    queue_count = 0U;
}

static ProductResult PopResult(void)
{
    if (queue_count == 0U)
    {
        return PRODUCT_UNKNOWN;
    }

    ProductResult result = queue[queue_read];

    queue_read = (queue_read + 1U) % QUEUE_SIZE;
    queue_count--;
    sorter_results_consumed++;

    return result;
}

static void EnterError(SorterError error)
{
    if (state == ERROR_STATE)
    {
        return;
    }

    Conveyor_Stop();

    last_error = error;
    state = ERROR_STATE;

    Communication_ReportError(error);
    Communication_ReportState("ERROR_STOP");
}

static void StartProduct(
    ProductResult result,
    uint32_t now)
{
    current_result = result;

    if (result == PRODUCT_NORMAL)
    {
        Servo_SetPulseUs(NORMAL_US);
    }
    else
    {
        Servo_SetPulseUs(DEFECT_US);
    }

    started_at = now;
    state = WAIT_EXIT_ON;
}

void Sorter_Init(void)
{
    sorter_results_accepted = 0U;
    sorter_results_rejected = 0U;
    sorter_results_consumed = 0U;
    ClearProducts();

    normal_count = 0U;
    defect_count = 0U;

    last_error = SORTER_ERR_NONE;
    wifi_stop_error = 0U;
    state = WAIT_FRONT;
    front_previous = 0U;
    front_candidate = Tracking_IsDetected1();
    front_stable = 0U;
    started_at = HAL_GetTick();
    front_changed_at = started_at;

    // 처음 초기화할 때만 PWM 시작
    Servo_Init();
    Servo_SetPulseUs(HOME_US);

    LED_SetColor(LED_OFF);
}

uint8_t Sorter_SetResult(ProductResult result)
{
    // Receive into FIFO even while stopped; reset still clears the queue.

    if (result != PRODUCT_NORMAL &&
        result != PRODUCT_DEFECT)
    {
        sorter_results_rejected++;
        return 0U;
    }

    if (queue_count >= QUEUE_SIZE)
    {
        EnterError(SORTER_ERR_QUEUE_FULL);
        sorter_results_rejected++;
        return 0U;
    }

    queue[queue_write] = result;
    queue_write = (queue_write + 1U) % QUEUE_SIZE;
    queue_count++;
    sorter_results_accepted++;
    return 1U;
}

void Sorter_Stop(void)
{
    Conveyor_Stop();

    if (state != ERROR_STATE && state != STOPPED)
    {
        state = STOPPED;
        Communication_ReportState("MANUAL_STOP");
    }
}

uint8_t Sorter_IsStopped(void)
{
    return state == STOPPED || state == ERROR_STATE;
}

uint8_t Sorter_TryReset(void)
{
    if (!Sorter_IsStopped())
    {
        return 0U;
    }

    // 제품 정리 후에만 재시작 허용
    if (Tracking_IsDetected1() ||
        Tracking_IsDetected2() ||
        Tracking_IsDetected3())
    {
        return 0U;
    }

    // 누적 완료 수는 유지, 대기 정보는 초기화
    ClearProducts();

    last_error = SORTER_ERR_NONE;
    wifi_stop_error = 0U;
    front_previous = 0U;
    front_candidate = Tracking_IsDetected1();
    front_stable = 0U;
    started_at = HAL_GetTick();
    front_changed_at = started_at;

    // PWM 시작 함수를 다시 호출하지 않음
    Servo_SetPulseUs(HOME_US);

    state = WAIT_FRONT;
    LED_SetColor(LED_OFF);

    return 1U;
}

SorterError Sorter_GetError(void)
{
    return last_error;
}

uint8_t Sorter_GetPendingCount(void)
{
    return queue_count + waiting_product;
}

uint32_t Sorter_GetNormalCount(void)
{
    return normal_count;
}

uint32_t Sorter_GetDefectCount(void)
{
    return defect_count;
}

void Sorter_Update(void)
{
    uint32_t now = HAL_GetTick();

    // Accept both detection and release only after a stable 50ms.
    uint8_t front_raw = Tracking_IsDetected1();
    if (front_raw != front_candidate)
    {
        front_candidate = front_raw;
        front_changed_at = now;
    }
    if (now - front_changed_at >= FRONT_FILTER_MS)
    {
        front_stable = front_candidate;
    }
    uint8_t front = front_stable;
    uint8_t left = Tracking_IsDetected2();
    uint8_t right = Tracking_IsDetected3();

    if (state != ERROR_STATE)
    {
        error_front = front;
        error_left = left;
        error_right = right;
        error_result = current_result;
    }

    uint8_t arrived =
        (front_previous == 0U && front == 1U);

    front_previous = front;

    // 현재 판정에 맞는 출구와 반대 출구
    uint8_t expected = 0U;
    uint8_t wrong = 0U;

    if (current_result == PRODUCT_NORMAL)
    {
        expected = left;
        wrong = right;
    }
    else if (current_result == PRODUCT_DEFECT)
    {
        expected = right;
        wrong = left;
    }

    // 처리 중 반대 출구 감지: 즉시 오류
    if ((state == WAIT_EXIT_ON ||
         state == WAIT_EXIT_OFF ||
         state == CLEARANCE) && wrong)
    {
        EnterError(SORTER_ERR_ROUTE_MISMATCH);
    }

    // 앞 센서에서 새 제품 감지
    if (arrived && !Sorter_IsStopped())
    {
        if (state == WAIT_FRONT && (left || right))
        {
            EnterError(SORTER_ERR_EXIT_OCCUPIED);
        }
        else if (state != WAIT_FRONT && waiting_product)
        {
            EnterError(SORTER_ERR_TOO_CLOSE);
        }
        else if (queue_count == 0U)
        {
            EnterError(SORTER_ERR_NO_RESULT);
        }
        else
        {
            ProductResult result = PopResult();

            if (state == WAIT_FRONT)
            {
                StartProduct(result, now);

                // 새 제품의 판정으로 출구 상태 갱신
                expected = result == PRODUCT_NORMAL
                           ? left : right;

                wrong = result == PRODUCT_NORMAL
                        ? right : left;
            }
            else
            {
                waiting_result = result;
                waiting_product = 1U;
            }
        }
    }

    switch (state)
    {
        case WAIT_FRONT:
            // 제품 없이 출구에 감지가 생겨도 오류
            if (left || right)
            {
                EnterError(SORTER_ERR_EXIT_OCCUPIED);
            }
            break;

        case WAIT_EXIT_ON:
            if (wrong)
            {
                EnterError(SORTER_ERR_ROUTE_MISMATCH);
            }
            else if (expected)
            {
                started_at = now;
                state = WAIT_EXIT_OFF;
            }
            else if (now - started_at >= TIMEOUT_MS)
            {
                EnterError(SORTER_ERR_EXIT_TIMEOUT);
            }
            break;

        case WAIT_EXIT_OFF:
            if (!expected)
            {
                // 올바른 출구 감지 → 해제 완료
                // 상태를 변경하므로 한 번만 집계
                if (current_result == PRODUCT_NORMAL)
                {
                    normal_count++;
                }
                else
                {
                    defect_count++;
                }

                Communication_ReportDone(
                    current_result,
                    normal_count,
                    defect_count
                );

                started_at = now;
                state = CLEARANCE;
            }
            else if (now - started_at >= EXIT_STUCK_MS)
            {
                EnterError(SORTER_ERR_EXIT_STUCK);
            }
            break;

        case CLEARANCE:
            if (left || right)
            {
                EnterError(SORTER_ERR_TOO_CLOSE);
            }
            else if (now - started_at >= CLEARANCE_MS)
            {

                if (waiting_product)
                {
                    ProductResult next = waiting_result;

                    waiting_product = 0U;
                    waiting_result = PRODUCT_UNKNOWN;

                    // 다른 방향이면 다음 방향으로 이동
                    StartProduct(next, now);
                }
                else
                {
                    // // 다음 판정이 같으면 중앙 복귀 생략
                    // if (PeekResult() != completed)
                    // {
                    //     Servo_SetPulseUs(HOME_US);
                    // }

                    Servo_SetPulseUs(HOME_US);
                    current_result = PRODUCT_UNKNOWN;
                    state = WAIT_FRONT;
                }
            }
            break;

        case STOPPED:
        case ERROR_STATE:
            break;
    }

    // LED: 현재 제품 판정과 정지 상태 표시
    switch (state)
    {
        case WAIT_FRONT:
            LED_SetColor(LED_OFF);
            break;

        case WAIT_EXIT_ON:
            LED_SetColor(LED_BLUE);
            break;

        case WAIT_EXIT_OFF:
        case CLEARANCE:
            LED_SetColor(
                current_result == PRODUCT_NORMAL
                    ? LED_GREEN : LED_RED
            );
            break;

        case STOPPED:
        case ERROR_STATE:
            LED_SetColor(
                ((now / 250U) % 2U)
                    ? LED_RED : LED_OFF
            );
            break;
    }
    }
ProductResult Sorter_GetCurrentResult(void)
{
    return current_result;
}

const char *Sorter_GetPhaseText(void)
{
    switch (state)
    {
        case WAIT_EXIT_ON:
            return "SORT";

        case WAIT_EXIT_OFF:
            return "EXIT";

        case CLEARANCE:
            return "CLEAR";

        case STOPPED:
            return "STOP";

        case ERROR_STATE:
            return "ERROR";

        case WAIT_FRONT:
        default:
            return "IDLE";
    }
}