#include "uart_test.h"


#define UART_RX_BUF_SIZE 128
#define BAUDRATE         115200
#define PERIPHERAL_CLOCK 8000000


typedef struct {
	uint8_t buf[UART_RX_BUF_SIZE];
	volatile uint16_t head;
	volatile uint16_t tail;
} uart_ring_buffer_t;


static uart_ring_buffer_t uart1_rx;


static void uart_rx_push(uint8_t data)
{
	uint16_t next = (uart1_rx.head + 1) % UART_RX_BUF_SIZE;

	if (next != uart1_rx.tail)
	{
		uart1_rx.buf[uart1_rx.head] = data;
		uart1_rx.head = next;
	}
	// else: BUFFER FULL -> BYTE DROPPED
}


void initClocks()
{
	// INITIALIZE ALL CLOCKS
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN; // Enable GPIO Port C Clock (Bit 2)
	RCC->APB2ENR |= RCC_APB2ENR_USART1EN; // Enable UART 1 Clock (Bit 14)
}


void configPinMode()
{
	// RESET PIN MODES
	GPIOC->MODER &= ~((3u << 8) | (3u << 10)); // Clear PC4 and PC5
	GPIOC->MODER |= ((2u << 8) | (2u << 10)); //Set PC4 and PC5 to Alternate Function (AF)

}


void setAF()
{
	// RESET ALTERNATE FUNCTION. AFR[0] IS THE ALTERNATE FUNCTION LOW REGISTER
	GPIOC->AFR[0] &= ~((0xF << 16) | (0xF << 20));

	// SET ALTERNATE FUNCTION
	GPIOC->AFR[0] |= ((7u << 16) | (7u << 20));

}


void configureUart1Pins()
{
	// SET PIN MODE TO ALTERNATE FUNCTION
	configPinMode();

	// SET ALTERNATE FUNCTION TO BE UART1
	setAF();
}


void configureUart1()
{
	// CLEAR BITS
	USART1->CR1 &= ~(USART_CR1_M1 // Clear M1 for 1 start bit and 8 data bits (28)
					| USART_CR1_OVER8 // Oversampling by 16 (15)
					| USART_CR1_CMIE // Inhibit character match interrupt (14)
					| USART_CR1_MME // Do not enable mute mode (13)
					| USART_CR1_M0 // Clear M0 for 1 start bit and 8 data bits (12)
					| USART_CR1_PS // Enable even parity (9)
					| USART_CR1_TE // Do not enable transmitter yet (3)
					| USART_CR1_RE // Do not enable receiver yet (2)
					| USART_CR1_UE); // Do not enable UART1 yet (0)

	// CLEAR BITS
	USART1->CR2 &= ~(USART_CR2_RTOEN // Disable receiver timeout (23)
					| USART_CR2_ABREN // No automatic baud rate detection (20)
					| USART_CR2_MSBFIRST // Transmit/Receive LSB first (19)
					| (0x03 << 16) // Idle state high for RX/TX pins (17/16)
					| USART_CR2_SWAP // Do not swap function of RX/TX pins (15)
					| USART_CR2_LINEN // No LIN mode (14)
					| (0x03 << 12) // 1 stop bit (13/12)
					| USART_CR2_CLKEN // Do not use clock with UART (11)
					| USART_CR2_LBDIE); // No LIN break detection interrupt (6)

	// CLEAR BITS
	USART1->CR3 &= ~(USART_CR3_TCBGTIE // No transmission complete before GUART time interrupt (24)
					| USART_CR3_DEM // No driver enable mode (14)
					| (0x7F << 3) // Disable various irrelevant modes (9-3)
					| USART_CR3_IREN // No IrDA mode (1)
					| USART_CR3_EIE); // Inhibit error interrupt (0)

	// SET BITS
	USART1->CR3 |= (USART_CR3_OVRDIS); // Disable overrun functionality (12)

	// ENABLE RX INTERRUPT
	USART1->CR1 |= USART_CR1_RXNEIE;

	// SET BAUD RATE IN BRR REGISTER
	USART1->BRR = BAUDRATE;

	//ENABLE UART
	USART1->CR1 |= (USART_CR1_TE // Enable transmitter (3)
					| USART_CR1_RE // Enable receiver (2)
					| USART_CR1_UE); // Enable UART1 (0)

	// ENABLE NVIC INTERRUPT
	NVIC_EnableIRQ(USART1_IRQn);
}


void USART1_IRQHandler()
{
	if (USART1->ISR & USART_ISR_RXNE)
	{
		uint8_t data = USART1->RDR;
		uart_rx_push(data);
	}

	// CLEAR ERROR FLAGS IF ANY
	if (USART1->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE))
	{
		volatile uint32_t tmp = USART1->RDR;
		(void)tmp;
	}
}


uint8_t receiveUart()
{
	uint8_t rxData = 0;

	// IF THERE IS DATA IN THE RECEIVE DATA REGISTER
	// USART_ISR_RXNE EXPANDS TO (1 << 5)
	if (uart1_rx.head != uart1_rx.tail)
	{
		// READ DATA FROM REGISTER
		rxData = uart1_rx.buf[uart1_rx.tail];
		uart1_rx.tail = (uart1_rx.tail + 1) % UART_RX_BUF_SIZE;
	}

	return rxData;
}


void transmitUart(uint8_t data)
{
	// WAIT UNTIL TRANSMITTER DATA REGISTER IS READY TO TAKE DATA
	// USART_ISR_TXE EXPANDS TO (1 << 7)
	while(!(USART1->ISR & USART_ISR_TXE));

	//LOAD DATA TO TRANSMIT REGISTER
	USART1->TDR = data;
}


void initUART()
{
	// INITIALISE REQUIRED CLOCKS. MUST ALWAYS BE DONE FIRST AS WITHOUT ENABLING THE CLOCKS THE PERIPHERALS CANNOT BE CONFIGURED OR USED
	initClocks();

	// CONFIGURE PINS TO UART1
	configureUart1Pins();

	//CONFIGURE UART1
	configureUart1();

}
