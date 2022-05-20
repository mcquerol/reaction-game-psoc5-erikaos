/**
* \file main
* \author Peter Fromm
* \date 5.12.2019
*
* \brief Demonstrator for critical regions
*
* The file contains two cyclic tasks, whioch both access the same UART to create a critical region
* Depending on priority and timing configuration, the data may get corrupted.
*
* \note <notes>
* \todo <todos>
* \warning <warnings, e.g. dependencies, order of execution etc.>
*
*  Changelog:\n
*  - <version; data of change; author>
*            - <description of the change>
*
* \copyright Copyright ©2016
* Department of electrical engineering and information technology, Hochschule Darmstadt - University of applied sciences (h_da). All Rights Reserved.
* Permission to use, copy, modify, and distribute this software and its documentation for educational, and research purposes in the context of non-commercial
* (unless permitted by h_da) and official h_da projects, is hereby granted for enrolled students of h_da, provided that the above copyright notice,
* this paragraph and the following paragraph appear in all copies, modifications, and distributions.
* Contact Prof.Dr.-Ing. Peter Fromm, peter.fromm@h-da.de, Birkenweg 8 64295 Darmstadt - GERMANY for commercial requests.
*
* \warning This software is a PROTOTYPE version and is not designed or intended for use in production, especially not for safety-critical applications!
* The user represents and warrants that it will NOT use or redistribute the Software for such purposes.
* This prototype is for research purposes only. This software is provided "AS IS," without a warranty of any kind.
*/


#include "project.h"
#include "global.h"
#include "trcRecorder.h"

#include "led.h"
#include "seven.h"
#include "joystick.h"
#include "tft.h"
#include "button.h"

#include "logging.h"

//Global handles for Tracealyser
traceHandle TRC_SystickHandle;
traceHandle TRC_ISRButtonHandle;

#undef TRC_SYSTICK


//Some statics, should be placd in a driver
static uint8_t BLINKER_left = 0;
static uint8_t BLINKER_right = 0;

typedef enum {STATE_OFF, STATE_LEFT_BLINK, STATE_LEFT_OVERTAKE, STATE_EMERGENCY} STATE_t;



//ISR which will increment the systick counter every ms
ISR(systick_handler)
{
#ifdef TRC_SYSTICK
    vTraceStoreISRBegin(TRC_SystickHandle);
#endif   
    
    CounterTick(cnt_systick);

#ifdef TRC_SYSTICK
    vTraceStoreISREnd(0);
#endif   

}

int main()
{
    CyGlobalIntEnable; /* Enable global interrupts. */
   
    //Set systick period to 1 ms. Enable the INT and start it.
	EE_systick_set_period(MILLISECONDS_TO_TICKS(1, BCLK__BUS_CLK__HZ));
	EE_systick_enable_int();
    
    //Enable trace
    vTraceEnable(TRC_INIT);
    TRC_SystickHandle = xTraceSetISRProperties("SysTick", 1);
    TRC_ISRButtonHandle = xTraceSetISRProperties("ISR_Button", 1);
   
    // Start Operating System
    for(;;)	    
    	StartOS(OSDEFAULTAPPMODE);
}

void unhandledException()
{
    //Ooops, something terrible happened....check the call stack to see how we got here...
    __asm("bkpt");
    ShutdownOS(0);
    while(1)
    {
    }
}

/********************************************************************************
 * Task Definitions
 ********************************************************************************/

TASK(tsk_init)
{
    
    //Init MCAL Drivers
    UART_LOG_Start();
    
    LED_Init();
    //SEVEN_Init();
    //JOYSTICK_Init();
    //TFT_init();
    //TFT_setBacklight(255);
    
    //TFT_print("Hello world\n");
        
    //Reconfigure ISRs with OS parameters.
    //This line MUST be called after the hardware driver initialisation!
    EE_system_init();
    
	
    //Start SysTick
	//Must be done here, because otherwise the isr vector is not overwritten yet
    EE_systick_start();  
	
    //Start the cyclic alarms 
    SetRelAlarm(alrm_cyclic_25,25,25);
    SetRelAlarm(alrm_cyclic_500,500,500);
    
    SetRelAlarm(alrm_trace,200,200); 

    //Activate all extended and the background task
    ActivateTask(tsk_control);
    ActivateTask(tsk_background);
    
    TerminateTask();
    
}

TASK(tsk_input)
{

    //Better: Put that into the OO-driver of the button
    static uint16_t button_1_time = 0;
    
    //We might add a flag to memorize the prev state - hacky.
    //Either leave it to the state machine or also include in driver
    
    if (TRUE == BUTTON_IsPressed(BUTTON_1))
    {
        SetEvent(tsk_control, ev_btn_left_down);
        button_1_time += 10;    
    }
    else
    {
        SetEvent(tsk_control, ev_btn_left_up);
        
        //This part is buggy! Why?
        if (button_1_time < 500)
        {
            SetEvent(tsk_control, ev_btn_left_short);
        }
        
        button_1_time = 0;
    }
    
    //Add the same for the right button
    
    //Emergency
    if (TRUE == BUTTON_IsPressed(BUTTON_3))
    {
        SetEvent(tsk_control, ev_emergency);
    }
    
    
    TerminateTask();
}

TASK(tsk_output)
{
    
    if (1 == BLINKER_left) LED_Toggle(LED_GREEN);
    if (1 == BLINKER_right) LED_Toggle(LED_RED);
    
    TerminateTask();
}

TASK(tsk_control)
{
 
    EventMaskType ev = 0;
    uint32_t delayInMs = 0;
    
    STATE_t state = STATE_OFF;
    

    
    while(1)
    {
        WaitEvent (ev_5s | ev_btn_left_down | ev_btn_left_short | ev_btn_left_up | ev_emergency);
        GetEvent(tsk_control, &ev);
        ClearEvent(ev);
        
        //While ev > 0
        //No 2 events will come in the same cyle
        
        switch (state)
        {
            case STATE_OFF :
            
                if (ev & ev_btn_left_down)
                {
                    LOG_I("ctrl","STATE__OFF, ev_btn_left_down");
                    
                    BLINKER_left = 1;
                    state = STATE_LEFT_BLINK;
                }
                else if (ev & ev_btn_left_short)
                {
                    
                    LOG_I("ctrl","STATE__OFF, ev_btn_left_short");
                    
                    BLINKER_left = 1;
                    
                    CancelAlarm(alrm_5000);
                    SetRelAlarm(alrm_5000, 5000, 0);
                    
                    state = STATE_LEFT_OVERTAKE;
                    
                }
                else if (ev & ev_emergency)
                {
                    
                    LOG_I("ctrl","STATE__OFF, ev_emergency");
                    
                    BLINKER_left = 1;
                    BLINKER_right = 1;
                    
                    state = STATE_EMERGENCY;
                    
                }
                
                break;
                    
            case STATE_LEFT_BLINK :
                if (ev & ev_btn_left_up)
                {
                    
                    LOG_I("ctrl","STATE_LEFT_BLINK, ev_btn_left_up");
                    
                    BLINKER_left = 0;
                    
                    state = STATE_OFF;
                }
                
                break;
                
            //Other States to be added
        }
                    
    
    }
    
    TerminateTask();
}

TASK(tsk_background)
{
    while(1)
    {
        //do something with low prioroty
        __asm("nop");
    }
}


/********************************************************************************
 * ISR Definitions
 ********************************************************************************/


ISR2(isr_Button)
{
    
    //The ISR does not really bring too much benefit in this project, as we need the task anyway
    vTraceStoreISRBegin(TRC_ISRButtonHandle);

    /*
    if (BUTTON_1_Read() == 1) SetEvent(tsk_event,ev_Button_1);   
    if (BUTTON_2_Read() == 1) SetEvent(tsk_event,ev_Button_2);   
    if (BUTTON_3_Read() == 1) SetEvent(tsk_event,ev_Button_3);   
    if (BUTTON_4_Read() == 1) SetEvent(tsk_event,ev_Button_4);   
    */
    vTraceStoreISREnd(0);
}

/* [] END OF FILE */
