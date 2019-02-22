/* ========================================
 *
 * Copyright YOUR COMPANY, THE YEAR
 * All Rights Reserved
 * UNPUBLISHED, LICENSED SOFTWARE.
 *
 * CONFIDENTIAL AND PROPRIETARY INFORMATION
 * WHICH IS THE PROPERTY OF your company.
 *
 * ========================================
*/
#include <project.h>
#include <stdio.h>

// USB device number.
#define USBFS_DEVICE  (0u)

// Packet size of USBUART
#define     UART_TX_QUEUE_SIZE      (64)
#define     UART_RX_QUEUE_SIZE      (64)

// BULK-IN/OUT parameters
#define     IN_EP                   (0x01u)
#define     OUT_EP                  (0x02u)
#define     MAX_PACKET_SIZE         (0x40u)
#define     TARGET_SIZE             (2052)
#define     OUT_NAKTIME             (100)

// TX Queue buffer for USBUART
uint8       uartTxQueue[UART_TX_QUEUE_SIZE];    // TX Queue buffer
uint8       uartTxCount = 0;                    // Data count in TX Queue
CYBIT       uartZlpRequired = 0;                // ZLP Request Flag
uint8       uartTxReject = 0;                   // TX Rejecting counter

// RX Queue buffer for USBUART
uint8       uartRxQueue[UART_RX_QUEUE_SIZE];    // RX Queue buffer
uint8       uartRxCount = 0;                    // Data count in RX Queue
uint8       uartRxIndex = 0;                    // Read index pointer

// TX Buffer for BULK-IN
uint8       buffer_in[MAX_PACKET_SIZE] = "ABCDEFGIHJKLMNOPQRSTUVWXYZ";

// RX Buffer for BULK-OUT
uint8       buffer_out[MAX_PACKET_SIZE];
uint32      rxCount = 0;
uint32      rxSize = 0;

// Periodically check the USBUART
CY_ISR(int_uartQueue_isr) {
    // TX control
    if ((uartTxCount > 0) || uartZlpRequired) {
        // Put TX buffer data
        if (USBFS_CDCIsReady()) {
            USBFS_PutData(uartTxQueue, uartTxCount);
            uartZlpRequired = (uartTxCount == UART_TX_QUEUE_SIZE);
            uartTxCount = 0;
            uartTxReject = 0;
        } else if (++uartTxReject > 4) {
            // Discard TX buffer
            uartTxCount = 0;
            uartTxReject = 0;
        }
    }
    // RX control
    if (uartRxIndex >= uartRxCount) {
        // Get data to empty RX buffer
        if (USBFS_DataIsReady()) {
            uartRxCount = USBFS_GetAll(uartRxQueue);
            uartRxIndex = 0;
        }
    }
}

static void putch_sub(const int16 ch) {
    for (;;) {                                  // 送信キューが空くまで待つ
        int_uartQueue_Disable();
        if (uartTxCount < UART_TX_QUEUE_SIZE) break;
        int_uartQueue_Enable();
    }
    uartTxQueue[uartTxCount++] = ch;            // 送信キューに一文字入れる
    int_uartQueue_Enable();
}

// USBUARTに一文字送る
void putch(const int16 ch) {
    if (ch == '\n') {
        putch_sub('\r');
    }
    putch_sub(ch);
}

// USBUARTから一文字受け取る
int16 getch(void) {
    int16 ch = -1;
    
    int_uartQueue_Disable();
    if (uartRxIndex < uartRxCount) {            // 受信キューに文字があるか確認
        ch = uartRxQueue[uartRxIndex++];        // 受信キューから一文字取り出す
        if (ch == '\r') {                       // 行末文字の変換処理
            ch = '\n';
        }
    }
    int_uartQueue_Enable();
    return ch;
}

void bulkOutDispatch(void) {
    char    numBuffer[32];
    uint16  length;

    if (USBFS_GetEPState(OUT_EP) & USBFS_OUT_BUFFER_FULL) {
        // Respond with NAK for a while
        CyDelayUs(OUT_NAKTIME);
        
        // Read received bytes count
        length = USBFS_GetEPCount(OUT_EP);

        // Unload the OUT buffer
        USBFS_ReadOutEP(OUT_EP, &buffer_out[0], length);
        
        if (length > MAX_PACKET_SIZE) {
            // Illegal packet size
            sprintf(numBuffer, "%ld - Illegal %dB\r\n", rxCount, length);
            UART_PutString(numBuffer);
            rxSize = 0;
            rxCount++;
        } else if (length == MAX_PACKET_SIZE) {
            // Max size packet
            rxSize += length;
        } else {
            // Short packet
            rxSize += length;
            if (rxSize != TARGET_SIZE) {
                sprintf(numBuffer, "%ld - %ldB\r\n", rxCount, rxSize);
                UART_PutString(numBuffer);
                putch('*');
            }
            rxSize = 0;
            rxCount++;
        }
    }
}

void echoBackDispatch(void) {
    int16   ch;

    ch = getch();
    if (ch >= 0) {
        putch(ch);
        if (ch == '\n') {
            putch('*');
        }
    }
}

int main()
{

    CyGlobalIntEnable; /* Enable global interrupts. */
    
    UART_Start();

    // Start USBFS operation with 5V operation.
    USBFS_Start(USBFS_DEVICE, USBFS_5V_OPERATION);
    
    int_uartQueue_StartEx(int_uartQueue_isr);
    
    for(;;) {
        // Wait for Device to enumerate
        while (USBFS_GetConfiguration() == 0);
        UART_PutString("C\r\n");

        // Drop CHANGE flag
        USBFS_IsConfigurationChanged();
        USBFS_CDC_Init();

        // Enable OUT endpoint for receive data from Host */
        USBFS_EnableOutEP(OUT_EP);
        
        rxSize = 0;

        for (;;) {
            // Check configuration changes
            if (USBFS_IsConfigurationChanged()) {
                UART_PutString("c\r\n");
                break;
            }

            // BULK-OUT : Check for OUT data received
            bulkOutDispatch();
            
            // BULK-IN : Check for IN buffer is empty
            if (USBFS_GetEPState(IN_EP) & USBFS_IN_BUFFER_EMPTY) {
                // Load the IN buffer
                buffer_in[0]++;
                USBFS_LoadInEP(IN_EP, &buffer_in[0], MAX_PACKET_SIZE);
                UART_PutString("p\r\n");
            }
            
            // CDC-OUT : Check for input data from host.
            echoBackDispatch();
            
            // CDC-Control : Drop Line Change events.
            (void)USBFS_IsLineChanged();
        }
        
        // UNCONFIGURED
    }
}

/* [] END OF FILE */
