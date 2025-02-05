#include "Controller.h"
#include "I2CManager.h"
#include "UserMenu.h"
#include "TemperatureCalc.h"

/***********************************************************************************************************************
 * Definitions
 **********************************************************************************************************************/

#define MAX_ALLOWABLE_TEMP_DIFF 1.0f // start fan if the temperature at any probe is this far from the average
#define REASONABLE_TEMP_DIFF 0.5f    // Do not stop fan until no probe is this far from the average temperature

typedef enum state
{
    CTRL_COLLECT_DATA,
    CTRL_LOG_DATA,
    CTRL_DO_MATH,
    CTRL_ACTUATE_FRIDGE,
    CTRL_WAIT_FOR_TIMER,
    CTRL_FAILED

}Controller_State_t;

/***********************************************************************************************************************
 * Variables
 **********************************************************************************************************************/

// HAL handles. These should all be extern, as they are defined and initialized by cubeMX (the code generation tool) in main.c
extern TIM_HandleTypeDef htim3;

// state input/output data
static ActuatorCommands_t ActuatorCommands;
static UserSettings_t UserSettings;

static Controller_State_t currentState;
static bool periodHasPassed;
static DataBuffer_t DataBuffer;
static PushButtonStates_t PushButtonStates;


static float avgTemp;


/***********************************************************************************************************************
 * Prototypes
 **********************************************************************************************************************/

static void StartPeriodTimer(void);

static Controller_State_t CollectData_State(void);
static Controller_State_t LogData_State(void);
static Controller_State_t DoMath_State(void);
static Controller_State_t ActuateFridge_State(void);
static Controller_State_t WaitForTimer_State(void);
static Controller_State_t Failed_State(void);

/***********************************************************************************************************************
 * Code
 **********************************************************************************************************************/

void Controller_Init(void)
{
    // let things settle
    HAL_Delay(1000);

    StartPeriodTimer();

    I2CManager_Init();

    UserMenu_Init();

    // let things settle
    HAL_Delay(1000);

    currentState = CTRL_COLLECT_DATA;
}

void Controller_SaveTheAfricans(void)
{
    Controller_State_t nextState;
    switch(currentState)
    {
        case CTRL_COLLECT_DATA:
            nextState = CollectData_State();
            break;

        case CTRL_LOG_DATA:
            nextState = LogData_State();
            break;

        case CTRL_DO_MATH:
            nextState = DoMath_State();
            break;

        case CTRL_ACTUATE_FRIDGE:
            nextState = ActuateFridge_State();
            break;

        case CTRL_WAIT_FOR_TIMER:
            nextState = WaitForTimer_State();
            break;

        case CTRL_FAILED:
            nextState = Failed_State();
            break;

        default:
            nextState = CTRL_FAILED;
            break;
    }

    currentState = nextState;
}

static Controller_State_t CollectData_State(void)
{
    UserMenu_GetUserSettings(&UserSettings);
    I2CManager_GetPushButtonStates(&PushButtonStates);

    Temperature_ADCtoCelsius(&DataBuffer);

    return CTRL_LOG_DATA;
}

static Controller_State_t LogData_State(void)
{
    char LCDString[16];

    UserMenu_DetermineLCDString(&PushButtonStates, avgTemp, LCDString);

    I2CManager_SendToLCD(LCDString);

    I2CManager_LaunchExchange();

    return CTRL_DO_MATH;
}

static Controller_State_t DoMath_State(void)
{
    static uint32_t numLoopsSinceCompressorOff;
    numLoopsSinceCompressorOff ++;

    avgTemp = 0;
    for (int i = 0; i < NUM_TEMP_PROBES; i++)
    {
        avgTemp += DataBuffer.temperature[i]/NUM_TEMP_PROBES;
    }

   /****************************Fan Control****************************/

    bool allProbesReasonableDiff = true;

    for (int i = 0; i < NUM_TEMP_PROBES; i++)
    {
        int tempDiff = DataBuffer.temperature[i] - avgTemp;

        if (ABS(tempDiff) > MAX_ALLOWABLE_TEMP_DIFF)
        {
            allProbesReasonableDiff = false;
            ActuatorCommands.fan = FAN_ON; // turn ON if any exceed the max allowable threshold
            break;
        }
        else if (ABS(tempDiff) > REASONABLE_TEMP_DIFF)
        {
            allProbesReasonableDiff = false;
        }
    }

    if (allProbesReasonableDiff)   // only turn off fan once all probes are within the reasonable threshold
    {
        ActuatorCommands.fan = FAN_OFF;
    }

    /****************************Compressor Control****************************/

    if (avgTemp > UserSettings.setTemp)
    {
        // Compressor must be off for at least 75s before being turned on again
        if(numLoopsSinceCompressorOff >= 75*CTRL_LOOP_FREQUENCY)
        {
            ActuatorCommands.compressor = COMPRESSOR_ON;
        }
    }
  
    else if (avgTemp < UserSettings.setTemp)
    {
        ActuatorCommands.compressor = COMPRESSOR_OFF;
        numLoopsSinceCompressorOff = 0;
    }
  
    return CTRL_ACTUATE_FRIDGE;
}

static Controller_State_t ActuateFridge_State(void)
{
    I2CManager_SendActuatorCommands(&ActuatorCommands);

    return CTRL_WAIT_FOR_TIMER;
}

static Controller_State_t WaitForTimer_State(void)
{
    if (periodHasPassed)
    {
        periodHasPassed = false;

        return CTRL_COLLECT_DATA;
    }

    return CTRL_WAIT_FOR_TIMER;
}

static Controller_State_t Failed_State(void)
{
    return CTRL_FAILED;
}

static void StartPeriodTimer(void)
{
    HAL_TIM_Base_Start_IT(&htim3);
}


/****************************Interrupt Handlers****************************/

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    periodHasPassed = true;
}
