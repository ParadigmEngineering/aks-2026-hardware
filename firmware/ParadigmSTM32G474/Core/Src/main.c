/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "uart_test.h"
#include "drive_can.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
FDCAN_HandleTypeDef hfdcan2;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

FDCAN_TxHeaderTypeDef TxHeader;
FDCAN_RxHeaderTypeDef RxHeader;

uint32_t TxMailbox;

uint8_t TxData[8];
uint8_t RxData[8];

uint32_t msg_count = 0;



static int16_t g_target_current = 0;
static int16_t g_target_speed = 0;
static DriveCommand_t g_command = CMD_HALTED;

static uint8_t ramp_step = 1;
static int16_t ramp_speed_rpm = 0;
static uint32_t ramp_last_tick = 0;

extern DriveController_t drive;



typedef struct {
    float    actual_speed_rpm;
    float    commanded_speed_rpm;
    float    actual_current_A;
    float    bus_voltage_V;
    float    ctrl_temp_C;
    float    motor_temp_C;
    uint8_t  life_signal;
    bool     is_running;
    bool     is_speed_mode;
    bool     handshake_done;
    uint32_t last_tx_tick;
} FakeMCU_t;

static FakeMCU_t mcu = {
    .bus_voltage_V = 48.0f,
    .ctrl_temp_C   = 25.0f,
    .motor_temp_C  = 25.0f,
};

/*
 * Transmit one extended CAN frame from the fake MCU side.
 * Uses the same FDCAN peripheral as the VCU — in loopback mode
 * both sets of frames are received back.
 */
static bool FakeMCU_Transmit(uint32_t id, const uint8_t *data)
{
    FDCAN_TxHeaderTypeDef hdr = {0};
    hdr.Identifier          = id;
    hdr.IdType              = FDCAN_EXTENDED_ID;
    hdr.TxFrameType         = FDCAN_DATA_FRAME;
    hdr.DataLength          = FDCAN_DLC_BYTES_8;
    hdr.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    hdr.BitRateSwitch       = FDCAN_BRS_OFF;
    hdr.FDFormat            = FDCAN_CLASSIC_CAN;
    hdr.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    hdr.MessageMarker       = 0;

    return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan2, &hdr, (uint8_t *)data) == HAL_OK;
}

/* Update fake MCU with latest VCU command */
static void FakeMCU_ReceiveCommand()
{
    mcu.commanded_speed_rpm = (float)g_target_speed - 32000.0f;
    mcu.is_running          = (g_command & CMD_RUNNING)    != 0;
    mcu.is_speed_mode       = (g_command & CMD_SPEED_MODE) != 0;
}

/*
 * Periodic fake MCU tick — call every loop iteration.
 * Internally gates to 50ms.
 *
 * Transmits real FDCAN frames that loop back through the filters
 * and fire the Rx callbacks
 */
static void FakeMCU_Tick()
{
    uint32_t now = HAL_GetTick();
    if ((now - mcu.last_tx_tick) < 50) { return; }
    mcu.last_tx_tick = now;

    /* -----------------------------------------------------------
     * Handshake: transmit 8 × 0x55 on Status1 ID until the VCU
     * transitions to OPERATIONAL
     * --------------------------------------------------------- */
    if (!mcu.handshake_done)
    {
        uint8_t hs[8];
        memset(hs, MCU_HANDSHAKE_BYTE, 8);

        /*
         * This frame travels through the FDCAN peripheral:
         *   TX FIFO → loopback → filter 0 (0x1801D0EF exact match)
         *   → FIFO0 → HAL_FDCAN_RxFifo0MsgPendingCallback
         *   → Drive_ProcessRx → state = OPERATIONAL
         */
        FakeMCU_Transmit(CAN_ID_MCU_STATUS1, hs);

        if (Drive_GetState() == DRIVE_STATE_OPERATIONAL)
        {
            mcu.handshake_done = true;
        }
        return;
    }

    /* -----------------------------------------------------------
     * Motor physics simulation
     *
     * Speed: first-order lag, 5% of remaining error per tick
     * Current: proportional to speed error
     * Temperature: rises with current, cools when idle
     * Voltage: sags under load
     * --------------------------------------------------------- */
    if (mcu.is_running && mcu.is_speed_mode)
    {
        float error          = mcu.commanded_speed_rpm - mcu.actual_speed_rpm;
        mcu.actual_speed_rpm += error * 0.05f;

        mcu.actual_current_A  = (error < 0.0f ? -error : error) * 0.01f;
        mcu.motor_temp_C     += (mcu.actual_current_A * 0.002f) - 0.001f;
        mcu.ctrl_temp_C      += (mcu.actual_current_A * 0.001f) - 0.001f;
    }
    else
    {
        mcu.actual_speed_rpm *= 0.90f;
        mcu.actual_current_A  = 0.0f;
        mcu.motor_temp_C     -= 0.01f;
        mcu.ctrl_temp_C      -= 0.005f;
    }

    if (mcu.motor_temp_C < 25.0f) { mcu.motor_temp_C = 25.0f; }
    if (mcu.ctrl_temp_C  < 25.0f) { mcu.ctrl_temp_C  = 25.0f; }
    mcu.bus_voltage_V = 48.0f - (mcu.actual_current_A * 0.05f);

    /* -----------------------------------------------------------
     * Transmit Status1: voltage, bus current, phase current, speed
     *
     * This frame travels:
     *   TX FIFO → loopback → filter 0 (0x1801D0EF)
     *   → FIFO0 → callback → Drive_ProcessRx → ParseStatus1
     *   → drive.status updated
     * --------------------------------------------------------- */
    {
        uint16_t raw_v  = (uint16_t)(mcu.bus_voltage_V / 0.1f);
        uint16_t raw_bi = (uint16_t)((mcu.actual_current_A / 0.1f) + 32000.0f);
        uint16_t raw_pi = (uint16_t)((mcu.actual_current_A / 0.1f) + 32000.0f);
        uint16_t raw_sp = (uint16_t)(mcu.actual_speed_rpm + 32000.0f);

        uint8_t s1[8] = {
            (uint8_t)(raw_v  & 0xFF), (uint8_t)(raw_v  >> 8),
            (uint8_t)(raw_bi & 0xFF), (uint8_t)(raw_bi >> 8),
            (uint8_t)(raw_pi & 0xFF), (uint8_t)(raw_pi >> 8),
            (uint8_t)(raw_sp & 0xFF), (uint8_t)(raw_sp >> 8),
        };
        FakeMCU_Transmit(CAN_ID_MCU_STATUS1, s1);
    }

    /* -----------------------------------------------------------
     * Transmit Status2: temperatures, status byte, life signal
     *
     * This frame travels:
     *   TX FIFO → loopback → filter 1 (0x1802D0EF)
     *   → FIFO1 → callback → Drive_ProcessRx → ParseStatus2
     *   → drive.status updated, life signal checked
     * --------------------------------------------------------- */
    {
        uint8_t raw_ct = (uint8_t)(mcu.ctrl_temp_C  + 40.0f);
        uint8_t raw_mt = (uint8_t)(mcu.motor_temp_C + 40.0f);
        uint8_t status = (mcu.is_running    ? 0x01 : 0x00)
                       | (mcu.is_speed_mode ? 0x02 : 0x00);

        uint8_t s2[8] = {
            raw_ct,
            raw_mt,
            status,
            0x00, 0x00, 0x00,   /* no error flags */
            0x00,
            mcu.life_signal++   /* must increment — driver checks this */
        };
        FakeMCU_Transmit(CAN_ID_MCU_STATUS2, s2);
    }
}

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_FDCAN2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */



/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_FDCAN2_Init();
  /* USER CODE BEGIN 2 */
  initUART();

  if (!Drive_Init(&hfdcan2, MCU_ADDRESS_DEFAULT)) {
	  Error_Handler();
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  /*
	  uint8_t tx = 0x55;
	  uint8_t rx = 0;

	  transmitUart(tx);

	  while ((USART1->ISR & USART_ISR_RXNE) == 0); // Wait until something arrives

	  rx = receiveUart();

	  if (rx == tx)
	  {
		  HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
		  HAL_Delay(2000);
	  }
	*/
	  /* Run the fake MCU every iteration — internally gates to 50ms */
	          FakeMCU_Tick();

	          /* Safety: halt on fault or comms failure */
	          if (Drive_GetState() == DRIVE_STATE_COMM_FAILED || Drive_HasError())
	          {
	              g_target_current = DRIVE_ENCODE_CURRENT(0.0f);
	              g_target_speed   = DRIVE_ENCODE_SPEED(0.0f);
	              g_command        = CMD_HALTED;
	              FakeMCU_ReceiveCommand();
	              HAL_Delay(10);
	              continue;
	          }

	          uint32_t now = HAL_GetTick();

	          switch (ramp_step)
	          {
	              /* Step 0: wait for handshake */
	              case 0:
	                  g_target_current = DRIVE_ENCODE_CURRENT(0.0f);
	                  g_target_speed   = DRIVE_ENCODE_SPEED(0.0f);
	                  g_command        = CMD_HALTED;
	                  if (Drive_GetState() == DRIVE_STATE_OPERATIONAL)
	                  {
	                      ramp_speed_rpm = 0;
	                      ramp_last_tick = now;
	                      ramp_step      = 1;
	                  }
	                  break;

	              /* Step 1: ramp up 0 → 500rpm (+10rpm every 100ms = 5s) */
	              case 1:
	                  if (now - ramp_last_tick >= 100)
	                  {
	                      ramp_last_tick  = now;
	                      ramp_speed_rpm += 10;
	                      if (ramp_speed_rpm >= 500)
	                      {
	                          ramp_speed_rpm = 500;
	                          ramp_step      = 2;
	                          ramp_last_tick = now;
	                      }
	                  }
	                  g_target_speed   = DRIVE_ENCODE_SPEED((float)ramp_speed_rpm);
	                  g_target_current = DRIVE_ENCODE_CURRENT(0.0f);
	                  g_command        = CMD_RUNNING | CMD_SPEED_MODE;
	                  break;

	              /* Step 2: hold 500rpm for 3 seconds */
	              case 2:
	                  g_target_speed   = DRIVE_ENCODE_SPEED(500.0f);
	                  g_target_current = DRIVE_ENCODE_CURRENT(0.0f);
	                  g_command        = CMD_RUNNING | CMD_SPEED_MODE;
	                  if (now - ramp_last_tick >= 3000)
	                  {
	                      ramp_last_tick = now;
	                      ramp_step      = 3;
	                  }
	                  break;

	              /* Step 3: ramp up 500 → 1000rpm */
	              case 3:
	                  if (now - ramp_last_tick >= 100)
	                  {
	                      ramp_last_tick  = now;
	                      ramp_speed_rpm += 10;
	                      if (ramp_speed_rpm >= 1000)
	                      {
	                          ramp_speed_rpm = 1000;
	                          ramp_step      = 4;
	                          ramp_last_tick = now;
	                      }
	                  }
	                  g_target_speed   = DRIVE_ENCODE_SPEED((float)ramp_speed_rpm);
	                  g_target_current = DRIVE_ENCODE_CURRENT(0.0f);
	                  g_command        = CMD_RUNNING | CMD_SPEED_MODE;
	                  break;

	              /* Step 4: hold 1000rpm for 3 seconds */
	              case 4:
	                  g_target_speed   = DRIVE_ENCODE_SPEED(1000.0f);
	                  g_target_current = DRIVE_ENCODE_CURRENT(0.0f);
	                  g_command        = CMD_RUNNING | CMD_SPEED_MODE;
	                  if (now - ramp_last_tick >= 3000)
	                  {
	                      ramp_last_tick = now;
	                      ramp_step      = 5;
	                  }
	                  break;

	              /* Step 5: ramp down 1000 → 0rpm */
	              case 5:
	                  if (now - ramp_last_tick >= 100)
	                  {
	                      ramp_last_tick  = now;
	                      ramp_speed_rpm -= 10;
	                      if (ramp_speed_rpm <= 0)
	                      {
	                          ramp_speed_rpm = 0;
	                          ramp_step      = 6;
	                          ramp_last_tick = now;
	                      }
	                  }
	                  g_target_speed   = DRIVE_ENCODE_SPEED((float)ramp_speed_rpm);
	                  g_target_current = DRIVE_ENCODE_CURRENT(0.0f);
	                  g_command        = CMD_RUNNING | CMD_SPEED_MODE;
	                  break;

	              /* Step 6: hold 0rpm for 3 seconds then loop */
	              case 6:
	                  g_target_speed   = DRIVE_ENCODE_SPEED(0.0f);
	                  g_target_current = DRIVE_ENCODE_CURRENT(0.0f);
	                  g_command        = CMD_RUNNING | CMD_SPEED_MODE;
	                  if (now - ramp_last_tick >= 3000)
	                  {
	                      ramp_speed_rpm = 0;
	                      ramp_last_tick = now;
	                      ramp_step      = 1;
	                  }
	                  break;

	              default:
	                  ramp_step = 0;
	                  break;
	          }

	          FakeMCU_ReceiveCommand();
	          HAL_Delay(10);

  }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief FDCAN2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_FDCAN2_Init(void)
{

  /* USER CODE BEGIN FDCAN2_Init 0 */

  /* USER CODE END FDCAN2_Init 0 */

  /* USER CODE BEGIN FDCAN2_Init 1 */

  /* USER CODE END FDCAN2_Init 1 */
  hfdcan2.Instance = FDCAN2;
  hfdcan2.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  hfdcan2.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan2.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan2.Init.AutoRetransmission = ENABLE;
  hfdcan2.Init.TransmitPause = DISABLE;
  hfdcan2.Init.ProtocolException = DISABLE;
  hfdcan2.Init.NominalPrescaler = 4;
  hfdcan2.Init.NominalSyncJumpWidth = 1;
  hfdcan2.Init.NominalTimeSeg1 = 13;
  hfdcan2.Init.NominalTimeSeg2 = 2;
  hfdcan2.Init.DataPrescaler = 1;
  hfdcan2.Init.DataSyncJumpWidth = 1;
  hfdcan2.Init.DataTimeSeg1 = 1;
  hfdcan2.Init.DataTimeSeg2 = 1;
  hfdcan2.Init.StdFiltersNbr = 0;
  hfdcan2.Init.ExtFiltersNbr = 2;
  hfdcan2.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN2_Init 2 */

  /* USER CODE END FDCAN2_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {

  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
