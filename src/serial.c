/*
 * Copyright (c) Michael Tharp <gxti@partiallystapled.com>
 *
 * This file is distributed under the terms of the MIT License.
 * See the LICENSE file at the top of this tree, or if it is missing a copy can
 * be found at http://opensource.org/licenses/MIT
 */


#include "common.h"
#include "init.h"
#include "serial.h"

serial_t Serial1;
serial_t Serial4;


static void
outqueue_init(queue_t *q, uint8_t *buf, uint8_t size) {
	q->flag = CoCreateFlag(1, 0);
	ASSERT(q->flag != E_CREATE_FAIL);
	q->p_bot = q->p_read = q->p_write = buf;
	q->p_top = buf + size;
	q->count = size;
}


static StatusType
outqueue_put(queue_t *q, uint8_t value, uint32_t timeout) {
	StatusType rc;
	uint32_t save;
	DISABLE_IRQ(save);
	while (q->count == 0) {
		RESTORE_IRQ(save);
		rc = CoWaitForSingleFlag(q->flag, 0);
		if (rc != E_OK) {
			return rc;
		}
		DISABLE_IRQ(save);
	}
	q->count--;
	*q->p_write++ = value;
	if (q->p_write == q->p_top) {
		q->p_write = q->p_bot;
	}
	RESTORE_IRQ(save);
	return E_OK;
}


static uint16_t
outqueue_getI(queue_t *q) {
	uint16_t ret;
	if (q->p_write == q->p_read && q->count != 0) {
		return NO_CHAR;
	}
	q->count++;
	ret = *q->p_read++;
	if (q->p_read == q->p_top) {
		q->p_read = q->p_bot;
	}
	isr_SetFlag(q->flag);
	return ret;
}


void
serial_start(serial_t *serial, USART_TypeDef *u, int speed) {
	serial->device = u;
	serial->speed = speed;
	serial->rx_char = NO_CHAR;
	if (u == USART1) {
		RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
		NVIC_SetPriority(USART1_IRQn, IRQ_PRIO_USART);
		NVIC_EnableIRQ(USART1_IRQn);
	} else if (u == USART2) {
		RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
		NVIC_SetPriority(USART2_IRQn, IRQ_PRIO_USART);
		NVIC_EnableIRQ(USART2_IRQn);
	} else if (u == USART3) {
		RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
		NVIC_SetPriority(USART3_IRQn, IRQ_PRIO_USART);
		NVIC_EnableIRQ(USART3_IRQn);
	} else if (u == UART4) {
		RCC->APB1ENR |= RCC_APB1ENR_UART4EN;
		NVIC_SetPriority(UART4_IRQn, IRQ_PRIO_USART);
		NVIC_EnableIRQ(UART4_IRQn);
	} else if (u == UART5) {
		RCC->APB1ENR |= RCC_APB1ENR_UART5EN;
		NVIC_SetPriority(UART5_IRQn, IRQ_PRIO_USART);
		NVIC_EnableIRQ(UART5_IRQn);
	} else {
		HALT();
	}
	serial_set_speed(serial);
	u->CR1 = 0
		| USART_CR1_UE
		| USART_CR1_TE
		| USART_CR1_RE
		| USART_CR1_RXNEIE
		;
	serial->mutex_id = CoCreateMutex();
	ASSERT(serial->mutex_id != E_CREATE_FAIL);
	serial->rx_flag = CoCreateFlag(1, 0);
	ASSERT(serial->rx_flag != E_CREATE_FAIL);
	outqueue_init(&serial->out_q, serial->out_buf, USART_TX_BUF);
}


void
serial_set_speed(serial_t *serial) {
	USART_TypeDef *u = serial->device;
	if (u == USART1) {
		/* FIXME: assuming PCLK2=HCLK */
		u->BRR = system_frequency / serial->speed;
	} else {
		/* FIXME: assuming PCLK1=HCLK/2 */
		u->BRR = system_frequency / 2 / serial->speed;
	}
}


char
serial_getc(serial_t *serial) {
	char ret;
	ASSERT(CoWaitForSingleFlag(serial->rx_flag, 0) == E_OK);
	ret = (char)serial->rx_char;
	serial->rx_char = NO_CHAR;
	return ret;
}


void
serial_putc(serial_t *serial, const char value) {
	CoEnterMutexSection(serial->mutex_id);
	outqueue_put(&serial->out_q, value, 0);
	serial->device->CR1 |= USART_CR1_TXEIE;
	CoLeaveMutexSection(serial->mutex_id);
}


void
serial_puts(serial_t *serial, const char *value) {
	CoEnterMutexSection(serial->mutex_id);
	while (*value) {
		outqueue_put(&serial->out_q, *value++, 0);
		serial->device->CR1 |= USART_CR1_TXEIE;
	}
	CoLeaveMutexSection(serial->mutex_id);
}


void
serial_write(serial_t *serial, const char *value, uint16_t size) {
	CoEnterMutexSection(serial->mutex_id);
	while (size--) {
		outqueue_put(&serial->out_q, *value++, 0);
		serial->device->CR1 |= USART_CR1_TXEIE;
	}
	CoLeaveMutexSection(serial->mutex_id);
}


static void
service_interrupt(serial_t *serial) {
	USART_TypeDef *u = serial->device;
	uint16_t sr, dr;
	sr = u->SR;
	dr = u->DR;

	if (sr & USART_SR_RXNE) {
		serial->rx_char = dr;
		isr_SetFlag(serial->rx_flag);
	}
	while (sr & USART_SR_TXE) {
		dr = outqueue_getI(&serial->out_q);
		if (dr == NO_CHAR) {
			u->CR1 &= ~USART_CR1_TXEIE;
			break;
		}
		u->DR = dr;
		sr = u->SR;
	}
}


void
USART1_IRQHandler(void) {
	CoEnterISR();
	service_interrupt(&Serial1);
	CoExitISR();
}


void
UART4_IRQHandler(void) {
	CoEnterISR();
	service_interrupt(&Serial4);
	CoExitISR();
}
