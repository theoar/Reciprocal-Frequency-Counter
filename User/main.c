#include "stm32f0xx.h" 
#include <stdio.h>

void initRefFreqCnt(void); //timer 2 - inicjalizacja timera zliczającego impulsy referencyjne (timer 32bitowy)
void initInputFreqCnt(void); //timer 3 master, 1 slave - inicjalizacja timera zliczającego impulsy wejściowe (połaczenie 2ch timerów - uzyskanie timera 32bitoweg)
void initGateCnt(void); //timer 17 - timer odmierzający czas pomiaru (100 ms)
void initGateLimitCnt(void); //timer 7 - timer odmierzający maksymalny czas pomiaru (150 ms)
void initGateResetGPIO(void); //inicjalizacja pinu wykorzystywanego do restartu przerzutnika D
void measure(void); //funkcja rozpoczynająca pomiar częstotliwości
void resetGate(void); //funkcja resetująca wyjście przerzutnika D
void initTriggerEndGPIO(void); //funkcja konfigurująca wejścia uC, jako wejście przerwania zewnętrzengo impulsu zakończenia pomiaru 

//możliwe stany pomiaru
#define FREQ_NOSTATE 0 			//stan IDLE - pomiar nieaktwny
#define FREQ_MEASURED 1 		//pomiar zakończony
#define FREQ_MEASURING 2		//pomiar w trakcie
#define FREQ_UNDERRANGE 3		//mierzona częstotliwość jest zbyt mała
#define FREQ_DC 4				//mierzony sygnał ma postać sygnału DC

double Freq = 0;								//zmienna przechowująca mierzoną częstotliwość
volatile uint8_t FreqMeterFlag = FREQ_NOSTATE;	//zmienna przechowująca obecny stan pomiaru

//stała określająca częstotliwość sygnału referencyjnego częstotliwości
#define REF_FREQ 48000000LL

//funkcja obsługi przerwania licznika ogarniczającego maksymalny czas pomiaru
//inicjalizacja licznika zawarta w funkcji  initGateLimitCnt(void)
void TIM7_IRQHandler()
{
	if(TIM_GetITStatus(TIM7, TIM_IT_Update)==SET)
	{		
		TIM_ClearITPendingBit(TIM7, TIM_IT_Update);		
		
		//jeżeli przekroczono maksymaly czas pomiaru i na wyjściu przerzutnika pojawił się
		//stan wysoki (tzn.: z wejścia mierzonej częstotliwości otrzymano co najmniej jeden impulsu)
		//pomiar jest przerywany oraz uznaje się, że mierzona częstotliwość jest zybt mała i niemożliwa do pomiaru
		//przy tak krótkim czasie pomiaru
		if(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_8)==1)
			FreqMeterFlag = FREQ_UNDERRANGE;
		//w przeciwnym przypadku, gdy na wyjście przertunika nie zmieniło stanu z 0 -> 1 
		//uznaje się, że sygnał wejściowy jest sygnałem DC
		else
			FreqMeterFlag = FREQ_DC;
		
		//skoro na przekroczona maksymalny czas to należy wymusić na wyjści przerzutnika D stan niski ręcznie
		//spowoduje to wyzwolenie się przerwania na obsługiwanego przez przerwanie EXTI4_15_IRQHandler(void) 
		resetGate();	
	}			
}

//funkcja obsługi przerwania z pinu wyjściowego przerztunika D, przerwanie ustawione na 
//wykrywanie zbocza opadającego, co odpowiada zakończeniu pomiaru
void EXTI4_15_IRQHandler(void) 
{
	if(EXTI_GetFlagStatus(EXTI_Line8)==SET)
	{
		EXTI_ClearITPendingBit(EXTI_Line8);
		TIM_Cmd(TIM7, DISABLE);		//pomiar zakończony poprawnie, należy zatem przerwać licznik limitujący maksymalny czas pomiaru
		
		//należy sprawdzić, czy przerwanie to zostało wywołane asynchroniczym restartem wyjścia przerztunika,
		//które może zostać wymuszone w obsłudze przerwania TIM7_IRQHandler() ogarniczającego maksymalny czas pomiaru,
		//czy też wynika z faktu, że pomiar zakończył się poprawnie;
		//jeśli nie ustawiono flag zbyt niskiej częstotliwości lub wykrycia sygnału DC oznacza to, że pomiar został wykonany poprawnie
		if(FreqMeterFlag!=FREQ_UNDERRANGE && FreqMeterFlag!=FREQ_DC)	
			FreqMeterFlag=FREQ_MEASURED;
	}	

}



int main(void)
{	
	//inicjalizacja poszczególnych elementów układu pomiarowego
	initGateCnt();			//inicjalizacja licznika mierzącego czas bramkowania
	initGateResetGPIO();	//inicjalizacja pinu do asynchronicznego restartu przerztunika D
	initRefFreqCnt();		//inicjalizacja licznika zliczającego referencyjną częstotliwość 
	initInputFreqCnt();		//inicjalizacja licznika zliaczającego mierzoną częstotliwość
	initTriggerEndGPIO();	//inicjalizacja pinu potrzebnego do wykrycia końca pomiaru
	initGateLimitCnt();		//inicjalizacja licznika ogarniczającego maksymalny czas pomiaru
			
	while (1) 
	{
		//switch - case zależny od aktualnego stanu pomiaru
		switch(FreqMeterFlag)
		{
			//w przypadku, gdy pomiar został wykonany poprawnie odczytywane są zawartości liczników oraz przliczana jest wartość częstotliwości
			case FREQ_MEASURED:
			{
				//obliczenie wartości częstotliwości, jako stosunku zliczonych impulów wejściowego syngału do zliczonych impulsów referencyjnego sgynału pomnożonego przez wartość częstotliwości sgynału referncyjnego
				//Freq = ( (Liczba zliczonych impulsów wejściowego sygnału) * (Wartość częstotliwości sygnału referencyjnego) )/(Liczba zliczonych impulsów referencyjnego sygnału)
				Freq = (((uint32_t)(TIM_GetCounter(TIM3) | (uint32_t)TIM_GetCounter(TIM1) << 16))*REF_FREQ)/(double)(TIM_GetCounter(TIM2));
				FreqMeterFlag = FREQ_NOSTATE;
			}				
			break;
			
			//wykrycie stanu sygnału DC
			case FREQ_DC:
			{	
				//@TO DO
				//Tutaj można umieścić kod obsługujący sytuajcę, w której na wejściu wykryto sygnał DC
				FreqMeterFlag = FREQ_NOSTATE;				
			}				
			break;
			
			//wykrycie zbyt niskiej częstotliwości
			case FREQ_UNDERRANGE:
			{
				//@TO DO
				//Tutaj można umieścić kod obsługujący sytuajcę, w której na wejściu wykryto zbyt małą wartość częstotliwości
				FreqMeterFlag = FREQ_NOSTATE;
			}				
			break;
			
			//stan IDLE
			case FREQ_NOSTATE:
			{
					measure();
			}				
			break;
			
		}

	
	}

}	

//konfiguracja licznika zliczającego częstotliwość referencyjną
void initRefFreqCnt(void) //timer 2 - licznik 32bitowy
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
	TIM_TimeBaseInitTypeDef RefFreqCntConf;
	TIM_TimeBaseStructInit(&RefFreqCntConf);
	RefFreqCntConf.TIM_Prescaler = 0;						//brak prescalera
	RefFreqCntConf.TIM_Period = UINT32_MAX;					//maksymalny zakres licznika 
	RefFreqCntConf.TIM_CounterMode = TIM_CounterMode_Up;	//tryb zliczania "w górę"
	RefFreqCntConf.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM2, &RefFreqCntConf);			
	
	//ustawienie licznika w trybie GATED MODE, który pozwala na zliczanie impulsów wejściowych tylko i wyłącznie, gdy na określonym pinie
	//widoczny jest stan wysoki, w tym przypadku licznik ten sterowany jest wyjściem Q przerztunika D, impulsy są zliczane jedynie w przypadku,
	//gdy na wyjści przerzutnika pojawi się stan wysoki
	TIM_SelectInputTrigger(TIM2, TIM_TS_TI1FP1);	
	TIM_InternalClockConfig(TIM2);		
	TIM_SelectSlaveMode(TIM2, TIM_SlaveMode_Gated);		
	
	//konfiguracja pinu niezbędnego do pracy w trybie GATED MODE
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE);
	GPIO_InitTypeDef GpioConf;
	GPIO_StructInit(&GpioConf);
	GpioConf.GPIO_Mode = GPIO_Mode_AF;
	GpioConf.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GpioConf.GPIO_Speed = GPIO_Speed_Level_3;
	GpioConf.GPIO_Pin = GPIO_Pin_0;
	GPIO_Init(GPIOA, &GpioConf);
	
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource0, GPIO_AF_2);	
	
	//uruchomienie licznika
	TIM_Cmd(TIM2, ENABLE);
}

//inicjalizacja liczników zliczających impulsy wejściowe mierzonej częstotliwości sygnału wejściowego
//liczniki te połączone są szeregowo, co pozwala na uzyskanie 32 bitowego licznika działającego w sposób programowy
//można oczywiście wykorzystać jednej licznik 16bitowy, a przepłeninia zliczać w sposób programowy w przeraniu,
//jednak w tym przypadku, jeśli istnieje taka możliwość, a licznik ten nie będzie wykorzystywany do innych celów,
//to warto skorzystać z takiego rozwiązania
void initInputFreqCnt(void) //timer 3 master, 1 slave
{
	//master T3 - konfig licznika MASTER zliczającego impulsy mierzonej częstotliwości
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
	TIM_TimeBaseInitTypeDef InputFreqMasterConf;
	TIM_TimeBaseStructInit(&InputFreqMasterConf);
	InputFreqMasterConf.TIM_Prescaler = 0;						//brak prescalera
	InputFreqMasterConf.TIM_Period = UINT16_MAX;				//maksymalny zakres licznika 
	InputFreqMasterConf.TIM_CounterMode = TIM_CounterMode_Up;	//tryb zliczania "w górę"
	TIM_TimeBaseInit(TIM3, &InputFreqMasterConf);
	
	//wybranie źródła taktującego licznik, jako zewnętrznego sygnału, bez filtracji, przy domyślnym narastającym zbocznu sygnału
	TIM_ETRClockMode2Config(TIM3, TIM_ExtTRGPSC_OFF, TIM_ExtTRGPolarity_NonInverted, 0);	
	
	//ustawienie licznika w trybie GATED MODE, który pozwala na zliczanie impulsów wejściowych tylko i wyłącznie, gdy na określonym pinie
	//widoczny jest stan wysoki, w tym przypadku licznik ten sterowany jest wyjściem Q przerztunika D, impulsy są zliczane jedynie w przypadku,
	//gdy na wyjści przerzutnika pojawi się stan wysoki
	TIM_SelectInputTrigger(TIM3, TIM_TS_TI1FP1);
	TIM_SelectSlaveMode(TIM3, TIM_SlaveMode_Gated);		
	
	//ustawienie triggera licznika potrzebnego do pracy w układzie szeregowego połączenia dwóch liczników - trigger
	//jest wyzwalany w przypadku, gdy nastąpi przepełnienie się licznika TIM3
	TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);
	//master T3
	
	//slave T1 - konfiguracja licznika SLAVE
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
	TIM_TimeBaseInitTypeDef InputFreqSlaveConf;	
	TIM_TimeBaseStructInit(&InputFreqSlaveConf);
	InputFreqSlaveConf.TIM_Prescaler = 0;						//brak prescalera
	InputFreqSlaveConf.TIM_Period = UINT16_MAX;					//maksymalny zakres licznika
	InputFreqSlaveConf.TIM_CounterMode = TIM_CounterMode_Up;	//tryb zliczania "w górę"
	TIM_TimeBaseInit(TIM1, &InputFreqSlaveConf);
	
	//wybranie trybu SLAVE TRIGGER MODE
	TIM_SelectSlaveMode(TIM1, TIM_SlaveMode_Trigger);	
	//ustawienie sygnału przepełnienia z licznika TIM3 (MASTER), jako źródła zegarowego licznika TIM1
	TIM_ITRxExternalClockConfig(TIM1,TIM_TS_ITR2);				
	//slave T1
		
	//master T3 - pin Conf
	//konfiguracja pinów: 
	//a) pinu bramkującego licznik TIM1 (tryb pracy GATED_MODE)
	//b) pinu będącego źródłem zegarowym licznika TIM1
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE);
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOD, ENABLE);
	GPIO_InitTypeDef GpioConf;
	GPIO_StructInit(&GpioConf);
	GpioConf.GPIO_Mode = GPIO_Mode_AF;
	GpioConf.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GpioConf.GPIO_Speed = GPIO_Speed_Level_3;
	GpioConf.GPIO_Pin =  GPIO_Pin_6;
	GPIO_Init(GPIOA, &GpioConf);
	
	GpioConf.GPIO_Pin =  GPIO_Pin_2;
	GPIO_Init(GPIOD, &GpioConf);
	
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_1);
	GPIO_PinAFConfig(GPIOD, GPIO_PinSource2, GPIO_AF_0);
	//master T3 - pinConf
	
	
	//uruchomienie liczników
	TIM_Cmd(TIM1, ENABLE);
	TIM_Cmd(TIM3, ENABLE);
} 
 
//konfiguracja licznika odmierzającego czas pomiaru

//aby ogarniczyć obsługę programową, która musiałaby być umieszczona w przerwaniu z tego licznika
//wykorzytano tryb ONE PULSE MODE w połączniu z trybem pracy PWM, pozwala to na uzyskanie układu, w którym
//generowany jest jednej impuls sygnału o narastającym zboczny trwającym określony czas - w tym przypadku jest to 100 ms
//tryb ONE PULSE MODE pozwala na automatyczne zatrzymanie pracy układu licznika po pierwszym przełenieniu
void initGateCnt(void) //timer 17
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM17, ENABLE);
	TIM_TimeBaseInitTypeDef RefFreqCntConf;
	TIM_TimeBaseStructInit(&RefFreqCntConf);
	RefFreqCntConf.TIM_Prescaler = 48000-1;					//ustawienie preskalera tak, aby odmierzać czas 1 ms
	RefFreqCntConf.TIM_Period = 100+2-1;					//ustawienie czasu trawania impulsu równego 100 ms
	RefFreqCntConf.TIM_CounterMode = TIM_CounterMode_Up;	//ustawienie trybu zliczania w górę
	RefFreqCntConf.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM17, &RefFreqCntConf);		
		
	TIM_InternalClockConfig(TIM17);							//wybranie wewnętrznego źródła zegarowego
	
	//konfiguracja wyjścia układu PWM licznika
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE);
	GPIO_InitTypeDef GpioConf;
	GPIO_StructInit(&GpioConf);
	GpioConf.GPIO_Mode = GPIO_Mode_AF;
	GpioConf.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GpioConf.GPIO_Speed = GPIO_Speed_Level_3;
	GpioConf.GPIO_Pin = GPIO_Pin_7;
	GPIO_Init(GPIOA, &GpioConf);
	
	//konfiguracja kanału PWM
	TIM_OCInitTypeDef Channel;
	TIM_OCStructInit(&Channel);
	Channel.TIM_OCMode = TIM_OCMode_PWM1;				//tryb pracy - PWM
	Channel.TIM_OCPolarity = TIM_OCPolarity_Low;		//polaryzacja kanału wyjściego PWM
	Channel.TIM_OutputState = TIM_OutputState_Enable;	//załaczenie kanału
			
	Channel.TIM_Pulse = 2-1;							
	TIM_OC1Init(TIM17, &Channel);
	
	TIM_CtrlPWMOutputs(TIM17, ENABLE);					//załączenie wyjścia kanałów PWM
	
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_5);	
	TIM_SelectOnePulseMode(TIM17, TIM_OPMode_Single);		//załączenie trby ONE PULSE MODE

}

//konfiguracja licznika ogarniczającego maksymalny czas pomiaru częstotliwości
void initGateLimitCnt(void) //timer 7
{	
	
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);
	TIM_TimeBaseInitTypeDef SingalGenConf;
	TIM_TimeBaseStructInit(&SingalGenConf);
	SingalGenConf.TIM_Prescaler = 48000-1;					//prescaler pozwalający na ustalenie zliczania co 1ms
	SingalGenConf.TIM_Period = 150-1;						//ustawienie odmierzanego czasu na 150 ms
	SingalGenConf.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM7, &SingalGenConf);
	
	TIM_InternalClockConfig(TIM7);							//wybranie wewnętrznego źródła zegarowego
	TIM_SelectOnePulseMode(TIM7, TIM_OPMode_Single);		//załączenie trby ONE PULSE MODE
	TIM_ITConfig(TIM7, TIM_IT_Update, ENABLE);				//aktywacja przerwania - przełnienie licznika
	TIM_ClearITPendingBit(TIM7, TIM_IT_Update);				//wyczyszczenie flagi przerwania przełeniania - flaga ustawia się automatycznie po załączeniu prawania
	
	//konfiguracja przerwnia w układzie NVIC
	NVIC_InitTypeDef Interrupt;
	Interrupt.NVIC_IRQChannel = TIM7_IRQn;
	Interrupt.NVIC_IRQChannelPriority = 0;
	Interrupt.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&Interrupt);
	
}

//inicjalizacja pinu służącego do asynchronicznego resetowania wyjścia przerzutnika D
void initGateResetGPIO(void)
{
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOC, ENABLE);
	GPIO_InitTypeDef GpioConf;
	GPIO_StructInit(&GpioConf);
	GpioConf.GPIO_Mode = GPIO_Mode_OUT;
	GpioConf.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GpioConf.GPIO_OType = GPIO_OType_PP;
	GpioConf.GPIO_Speed = GPIO_Speed_Level_1;
	GpioConf.GPIO_Pin =  GPIO_Pin_8;
	GPIO_Init(GPIOC, &GpioConf);

	GPIO_WriteBit(GPIOC, GPIO_Pin_8, 0);
	GPIO_WriteBit(GPIOC, GPIO_Pin_8, 1);
}

//funkcja służaca do resetowania wyjścia przerzutnika D
void resetGate(void)
{
	GPIO_WriteBit(GPIOC, GPIO_Pin_8, 0);
	GPIO_WriteBit(GPIOC, GPIO_Pin_8, 1);
}


//inicjalizacja pinu służacego do określenia, czy pomiar został zakończony
void initTriggerEndGPIO(void)
{
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);	
	GPIO_InitTypeDef GpioConf;
	GPIO_StructInit(&GpioConf);
	GpioConf.GPIO_Mode = GPIO_Mode_IN;
	GpioConf.GPIO_PuPd = GPIO_PuPd_UP;	
	GpioConf.GPIO_Speed = GPIO_Speed_Level_1;
	GpioConf.GPIO_Pin =  GPIO_Pin_8;
	GPIO_Init(GPIOB, &GpioConf);
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
	
	//konfiguracja przerwania
	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, EXTI_PinSource8);
	
	EXTI_InitTypeDef ExtiConf;
	ExtiConf.EXTI_Line = EXTI_Line8;
	ExtiConf.EXTI_LineCmd = ENABLE;
	ExtiConf.EXTI_Trigger = EXTI_Trigger_Falling;			//wybrania zboczna opadającego
	ExtiConf.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_Init(&ExtiConf);
	
	NVIC_InitTypeDef NvicConf;
	NvicConf.NVIC_IRQChannel = EXTI4_15_IRQn;
	NvicConf.NVIC_IRQChannelCmd = ENABLE;
	NvicConf.NVIC_IRQChannelPriority = 1;	
	NVIC_Init(&NvicConf);
	
}


//funkcja rozpoczynająca pomiar częstotliwości
void measure(void)
{
	FreqMeterFlag = FREQ_MEASURING; 		//ustawienie flagi pomiaru
	
	//wyłączenie wszystkich liczników potrzebnych do pomiaru
	TIM_Cmd(TIM7, DISABLE);					
	TIM_Cmd(TIM1, DISABLE);
	TIM_Cmd(TIM2, DISABLE);
	TIM_Cmd(TIM3, DISABLE);
	TIM_Cmd(TIM17, DISABLE);	
	
	//wyzerowanie zawartości liczników
	TIM_SetCounter(TIM7, 0);	
	TIM_SetCounter(TIM1, 0);
	TIM_SetCounter(TIM2, 0);
	TIM_SetCounter(TIM3, 0);
	TIM_SetCounter(TIM17, 0);	
	
	//w przypadku, gdyby flaga przerwania licznika ograniczającego maksymalny czas pomiaru była niewyzerowana
	//należy ją wyzerować
	if(TIM_GetITStatus(TIM7, TIM_IT_Update)==SET)
		TIM_ClearITPendingBit(TIM7, TIM_IT_Update);
	
	//załączenie liczników zliczających częstotliwości
	TIM_Cmd(TIM1, ENABLE);
	TIM_Cmd(TIM2, ENABLE);
	TIM_Cmd(TIM3, ENABLE);
	
	//zalączenie liczników w kolejności:
	// - licznika bramkującego  TIM17
	// - licznika ogarniczającego maksymalny czas pomiaru
	//teoretycznie można skonfigurować liczniki do pracy Master-Slave, który pozwoliłby na 
	//równoległe uruchomienie obu układów, jednak w tym przypadku jest to zbędne i można sobie pozowlić
	//na niewielką desynchronizację, ponieważ licznik TIM17 mierzy czas o wiele dłuższy niż licznik TIM7 tak,
	//więc niewielkie programowe opóźnienie nie zakłóci pomiaru
	TIM_Cmd(TIM17, ENABLE);
	TIM_Cmd(TIM7, ENABLE);
}
