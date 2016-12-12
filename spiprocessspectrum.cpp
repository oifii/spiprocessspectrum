///////////////////////////////////////////////////////////////////////
//	spectrum based audio process
//
//	2015may31, created by stephane.poirier@oifii.org (or spi@oifii.org)
//			   from spispectrumlive_pa
///////////////////////////////////////////////////////////////////////

#include <windows.h>

#include <windowsx.h> //for GET_X_LPARAM()

#include <stdio.h>
#include <math.h>
#include <malloc.h>
//#include "bass.h"
#include "portaudio.h"

#ifdef WIN32
#if PA_USE_ASIO
#include "pa_asio.h"
#endif
#endif

#include "resource.h"

#include <string>
#include <map>
using namespace std;

//fourier.h is the addon from audio programming book to the rfftw library,
//see audio programming book page 536 for details. in short, fourier.cpp
//wraps the rfftw by providing 2 functions: fft() and ifft().
//fourier.h also depends on libsndfile so it makes rfftw.lib depends on
//libsndfile for compiling (no need for linking it if you don't use it
//elsewhere).
//fft() function can only be called always with the same sample size N,
//this because within fft() implementation rfftw_create_plan() is called
//only once (the first time fft() is called).
#include <fourier.h> //in rfftw.lib (static library)

#include <ctime> //for random number initialization seed


#define SAMPLE_RATE  (44100)
#define FRAMES_PER_BUFFER (1024)
//#define FRAMES_PER_BUFFER (512)
//#define FRAMES_PER_BUFFER (256)
//#define FRAMES_PER_BUFFER (128)
//#define FRAMES_PER_BUFFER (64)
//#define NUM_CHANNELS    (1)
#define NUM_CHANNELS    (2)

//#define MAX_AUDIODATA (10240) //FRAMES_PER_BUFFER should never be larger than MAX_AUDIODATA
#define MAX_AUDIODATA (FRAMES_PER_BUFFER) //MAX_AUDIODATA can be set identical to FRAMES_PER_BUFFER 

// Select sample format. 
#if 1
#define PA_SAMPLE_TYPE  paFloat32
typedef float SAMPLE;
#define SAMPLE_SILENCE  (0.0f)
#define PRINTF_S_FORMAT "%.8f"
#elif 1
#define PA_SAMPLE_TYPE  paInt16
typedef short SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#elif 0
#define PA_SAMPLE_TYPE  paInt8
typedef char SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#else
#define PA_SAMPLE_TYPE  paUInt8
typedef unsigned char SAMPLE;
#define SAMPLE_SILENCE  (128)
#define PRINTF_S_FORMAT "%d"
#endif

/*
//#define SPECWIDTH 368	// display width
//#define SPECHEIGHT 127	// height (changing requires palette adjustments too)
#define SPECWIDTH 1000	// display width
#define SPECHEIGHT 64	// height (changing requires palette adjustments too)
*/
//int SPECWIDTH=1024;	// display width
//int SPECHEIGHT=512;	// display height 
int SPECWIDTH=FRAMES_PER_BUFFER;	// display width
int SPECHEIGHT=FRAMES_PER_BUFFER/2;	// display height 

BYTE global_alpha=200;

map<string,int> global_inputdevicemap;
map<string,int> global_outputdevicemap;

PaStream* global_stream;
PaStreamParameters global_inputParameters;
PaStreamParameters global_outputParameters;
PaError global_err;
string global_audioinputdevicename="";
string global_audiooutputdevicename="";
int global_inputAudioChannelSelectors[2];
int global_outputAudioChannelSelectors[2];
PaAsioStreamInfo global_asioInputInfo;
PaAsioStreamInfo global_asioOutputInfo;

FILE* pFILE= NULL;


float global_fSecondsProcess; //negative for always recording
DWORD global_timer=0;
int global_x=200;
int global_y=200;

HWND win=NULL;
DWORD timer=0;

//DWORD chan;
//HRECORD chan;	// recording channel

HDC specdc=0;
HBITMAP specbmp=0;
BYTE *specbuf;

int specmode=0,specpos=0; // spectrum mode (and marker pos for 2nd mode)

//new parameters
string global_classname="spiprocessspectrumclass";
string global_title="spiprocessspectrum (click to filter)";
string global_begin="begin.ahk";
string global_end="end.ahk";
int global_idcolorpalette=0;
int global_bands=20;

bool global_abort=false;
unsigned long framescount=0;

float* audioData[2];
float* fftaudioData[2];
float* fftmultiplier[2];

static int spectrumCallback( const void *inputBuffer, void *outputBuffer,
                             unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void *userData );

static int gNumNoInputs = 0;
// This routine will be called by the PortAudio engine when audio is needed.
// It may be called at interrupt level on some machines so don't do anything
// that could mess up the system like calling malloc() or free().
//
static int spectrumCallback( const void *inputBuffer, void *outputBuffer,
							 unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void *userData )
{
    SAMPLE *out = (SAMPLE*)outputBuffer;
    const SAMPLE *in = (const SAMPLE*)inputBuffer;
    unsigned int i;
    (void) timeInfo; // Prevent unused variable warnings.
    (void) statusFlags;
    (void) userData;

	//HDC dc;
	int x,y,y1;

	if(global_abort==true) return paAbort;

    if( inputBuffer == NULL )
    {
        for( i=0; i<framesPerBuffer; i++ )
        {
            *out++ = 0;  // left - silent
            *out++ = 0;  // right - silent 
        }
        gNumNoInputs += 1;
    }
    else
    {
		/*
        for( i=0; i<framesPerBuffer; i++ )
        {
            *out++ = *in++;  // left - clean 
            *out++ = *in++;  // right - clean 
        }
		*/
		///////////////////////////////////
		//re-order buffer with no interleaf
		///////////////////////////////////
		for( i=0; i<framesPerBuffer; i++ )
        {
			audioData[0][i]=*in++; //left
			audioData[1][i]=*in++; //right
        }

		////////////////
		//process buffer
		////////////////
		fft(audioData[0], fftaudioData[0], framesPerBuffer); //left
		fft(audioData[1], fftaudioData[1], framesPerBuffer); //right

		//display fft left channel (for a reference)
		framescount = framescount+framesPerBuffer;
		if(framescount>=SAMPLE_RATE/10)
		{ 
			framescount=0;
			// "normal" FFT display
			memset(specbuf,0,SPECWIDTH*SPECHEIGHT); //erase display
			for (x=0;x<SPECWIDTH/2;x++) 
			{
	#if 0
				y=sqrt(fftaudioData[0][x+1])*3*SPECHEIGHT-4; // scale it (sqrt to make low values more visible)
	#else
				y=fftaudioData[0][x+1]*10*SPECHEIGHT; // scale it (linearly)
	#endif
				if (y>SPECHEIGHT) y=SPECHEIGHT; // cap it
				if (x && (y1=(y+y1)/2)) // interpolate from previous to make the display smoother
					//while (--y1>=0) specbuf[y1*SPECWIDTH+x*2-1]=y1+1;
					while (--y1>=0) specbuf[y1*SPECWIDTH+x*2-1]=(127*y1/SPECHEIGHT)+1;
				y1=y;
				//while (--y>=0) specbuf[y*SPECWIDTH+x*2]=y+1; // draw level
				while (--y>=0) specbuf[y*SPECWIDTH+x*2]=(127*y/SPECHEIGHT)+1; // draw level
			}
			PostMessage(win, WM_USER+1, 0, 0);
		} 

		//edit fft
		/*
		//high pass filter
		for( i=0; i<framesPerBuffer/10; i++ )
        {
			fftaudioData[0][i]=0;
			fftaudioData[1][i]=0;
        }
		*/
		
		/*
		//low pass filter
		for( i=framesPerBuffer/10; i<framesPerBuffer; i++ )
        {
			fftaudioData[0][i]=0;
			fftaudioData[1][i]=0;
        }
		*/

		/*
		//some filter
		for( i=0; i<framesPerBuffer; i++ )
        {
			if (i%2!=0)
			{
				fftaudioData[0][i]=0;
				fftaudioData[1][i]=0;
			}
        }
		*/
		/*
		//some filter
		for( i=0; i<framesPerBuffer; i++)
		{
			fftaudioData[0][i]=abs(fftaudioData[0][i]);
			fftaudioData[1][i]=abs(fftaudioData[1][i]);
		}
		*/

		//no pass filter
		float fmax = 0.0;
		for( i=0; i<framesPerBuffer; i++ )
        {
			if(fftaudioData[0][i]>fmax) fmax=fftaudioData[0][i];
			//fftaudioData[0][i]=0;
			//fftaudioData[1][i]=0;
        }
		
		//add 1 frequency (will induce a sine wave in audioData)
		/*
		fftaudioData[0][framesPerBuffer/2]=fmax/2;
		fftaudioData[1][framesPerBuffer/2]=fmax/2;
		fftaudioData[0][framesPerBuffer/3]=fmax/2;
		fftaudioData[1][framesPerBuffer/3]=fmax/2;
		fftaudioData[0][framesPerBuffer/4]=fmax/2;
		fftaudioData[1][framesPerBuffer/4]=fmax/2;
		fftaudioData[0][framesPerBuffer/5]=fmax/2;
		fftaudioData[1][framesPerBuffer/5]=fmax/2;
		*/
		/*
		fftaudioData[0][framesPerBuffer/10]=fmax/2;
		fftaudioData[1][framesPerBuffer/10]=fmax/2;
		*/
		//amplify or attenuate frequencies
		for( i=0; i<framesPerBuffer; i++ )
        {
			fftaudioData[0][i]=fftaudioData[0][i]*fftmultiplier[0][i]; //left
			fftaudioData[1][i]=fftaudioData[1][i]*fftmultiplier[1][i]; //right
        }

		ifft(fftaudioData[0], audioData[0], framesPerBuffer); //left
		ifft(fftaudioData[1], audioData[1], framesPerBuffer); //right
		
		////////////////////////////////
		//re-order buffer with interleaf
		////////////////////////////////
        for( i=0; i<framesPerBuffer; i++ )
        {
			*out++ = audioData[0][i]; //left
			*out++ = audioData[1][i]; //right
		}

    }
    
    return paContinue;
}


bool SelectAudioInputDevice()
{
	const PaDeviceInfo* deviceInfo;
    int numDevices = Pa_GetDeviceCount();
    for( int i=0; i<numDevices; i++ )
    {
        deviceInfo = Pa_GetDeviceInfo( i );
		string devicenamestring = deviceInfo->name;
		global_inputdevicemap.insert(pair<string,int>(devicenamestring,i));
		if(pFILE) fprintf(pFILE,"id=%d, name=%s\n", i, devicenamestring.c_str());
	}

	int deviceid = Pa_GetDefaultInputDevice(); // default input device 
	map<string,int>::iterator it;
	it = global_inputdevicemap.find(global_audioinputdevicename);
	if(it!=global_inputdevicemap.end())
	{
		deviceid = (*it).second;
		//printf("%s maps to %d\n", global_audiodevicename.c_str(), deviceid);
		deviceInfo = Pa_GetDeviceInfo(deviceid);
		//assert(inputAudioChannelSelectors[0]<deviceInfo->maxInputChannels);
		//assert(inputAudioChannelSelectors[1]<deviceInfo->maxInputChannels);
	}
	else
	{
		/*
		for(it=global_devicemap.begin(); it!=global_devicemap.end(); it++)
		{
			printf("%s maps to %d\n", (*it).first.c_str(), (*it).second);
		}
		*/
		//Pa_Terminate();
		//return -1;
		//printf("error, audio device not found, will use default\n");
		//MessageBox(win,"error, audio device not found, will use default\n",0,0);
		deviceid = Pa_GetDefaultInputDevice();
	}


	global_inputParameters.device = deviceid; 
	if (global_inputParameters.device == paNoDevice) 
	{
		/*
		fprintf(stderr,"Error: No default input device.\n");
		//goto error;
		Pa_Terminate();
		fprintf( stderr, "An error occured while using the portaudio stream\n" );
		fprintf( stderr, "Error number: %d\n", global_err );
		fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( global_err ) );
		return Terminate();
		*/
		//MessageBox(win,"error, no default input device.\n",0,0);
		return false;
	}
	//global_inputParameters.channelCount = 2;
	global_inputParameters.channelCount = NUM_CHANNELS;
	global_inputParameters.sampleFormat =  PA_SAMPLE_TYPE;
	global_inputParameters.suggestedLatency = Pa_GetDeviceInfo( global_inputParameters.device )->defaultLowOutputLatency;
	//inputParameters.hostApiSpecificStreamInfo = NULL;

	//Use an ASIO specific structure. WARNING - this is not portable. 
	//PaAsioStreamInfo asioInputInfo;
	global_asioInputInfo.size = sizeof(PaAsioStreamInfo);
	global_asioInputInfo.hostApiType = paASIO;
	global_asioInputInfo.version = 1;
	global_asioInputInfo.flags = paAsioUseChannelSelectors;
	global_asioInputInfo.channelSelectors = global_inputAudioChannelSelectors;
	if(deviceid==Pa_GetDefaultInputDevice())
	{
		global_inputParameters.hostApiSpecificStreamInfo = NULL;
	}
	else if(Pa_GetHostApiInfo(Pa_GetDeviceInfo(deviceid)->hostApi)->type == paASIO) 
	{
		global_inputParameters.hostApiSpecificStreamInfo = &global_asioInputInfo;
	}
	else if(Pa_GetHostApiInfo(Pa_GetDeviceInfo(deviceid)->hostApi)->type == paWDMKS) 
	{
		global_inputParameters.hostApiSpecificStreamInfo = NULL;
	}
	else
	{
		//assert(false);
		global_inputParameters.hostApiSpecificStreamInfo = NULL;
	}
	return true;
}

bool SelectAudioOutputDevice()
{
	const PaDeviceInfo* deviceInfo;
    int numDevices = Pa_GetDeviceCount();
    for( int i=0; i<numDevices; i++ )
    {
        deviceInfo = Pa_GetDeviceInfo( i );
		string devicenamestring = deviceInfo->name;
		global_outputdevicemap.insert(pair<string,int>(devicenamestring,i));
		if(pFILE) fprintf(pFILE,"id=%d, name=%s\n", i, devicenamestring.c_str());
	}

	int deviceid = Pa_GetDefaultOutputDevice(); // default output device 
	map<string,int>::iterator it;
	it = global_outputdevicemap.find(global_audiooutputdevicename);
	if(it!=global_outputdevicemap.end())
	{
		deviceid = (*it).second;
		//printf("%s maps to %d\n", global_audiodevicename.c_str(), deviceid);
		deviceInfo = Pa_GetDeviceInfo(deviceid);
		//assert(inputAudioChannelSelectors[0]<deviceInfo->maxInputChannels);
		//assert(inputAudioChannelSelectors[1]<deviceInfo->maxInputChannels);
	}
	else
	{
		//Pa_Terminate();
		//return -1;
		//printf("error, audio device not found, will use default\n");
		//MessageBox(win,"error, audio device not found, will use default\n",0,0);
		deviceid = Pa_GetDefaultOutputDevice();
	}


	global_outputParameters.device = deviceid; 
	if (global_outputParameters.device == paNoDevice) 
	{
		//MessageBox(win,"error, no default output device.\n",0,0);
		return false;
	}
	//global_inputParameters.channelCount = 2;
	global_outputParameters.channelCount = NUM_CHANNELS;
	global_outputParameters.sampleFormat =  PA_SAMPLE_TYPE;
	global_outputParameters.suggestedLatency = Pa_GetDeviceInfo( global_outputParameters.device )->defaultLowOutputLatency;
	//outputParameters.hostApiSpecificStreamInfo = NULL;

	//Use an ASIO specific structure. WARNING - this is not portable. 
	//PaAsioStreamInfo asioInputInfo;
	global_asioOutputInfo.size = sizeof(PaAsioStreamInfo);
	global_asioOutputInfo.hostApiType = paASIO;
	global_asioOutputInfo.version = 1;
	global_asioOutputInfo.flags = paAsioUseChannelSelectors;
	global_asioOutputInfo.channelSelectors = global_outputAudioChannelSelectors;
	if(deviceid==Pa_GetDefaultOutputDevice())
	{
		global_outputParameters.hostApiSpecificStreamInfo = NULL;
	}
	else if(Pa_GetHostApiInfo(Pa_GetDeviceInfo(deviceid)->hostApi)->type == paASIO) 
	{
		global_outputParameters.hostApiSpecificStreamInfo = &global_asioOutputInfo;
	}
	else if(Pa_GetHostApiInfo(Pa_GetDeviceInfo(deviceid)->hostApi)->type == paWDMKS) 
	{
		global_outputParameters.hostApiSpecificStreamInfo = NULL;
	}
	else
	{
		//assert(false);
		global_outputParameters.hostApiSpecificStreamInfo = NULL;
	}
	return true;
}


void CALLBACK StopRecording(UINT uTimerID, UINT uMsg, DWORD dwUser, DWORD dw1, DWORD dw2)
{
	PostMessage(win, WM_DESTROY, 0, 0);
}





// window procedure
long FAR PASCAL SpectrumWindowProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m) {
		case WM_PAINT:
			if (GetUpdateRect(h,0,0)) {
				PAINTSTRUCT p;
				HDC dc;
				if (!(dc=BeginPaint(h,&p))) return 0;
				BitBlt(dc,0,0,SPECWIDTH,SPECHEIGHT,specdc,0,0,SRCCOPY);
				EndPaint(h,&p);
			}
			return 0;

		case WM_USER+1:
			{
				// update the display
				HDC dc=GetDC(win);
				BitBlt(dc,0,0,SPECWIDTH,SPECHEIGHT,specdc,0,0,SRCCOPY);
				ReleaseDC(win,dc);
			}
			return 0;
		case WM_LBUTTONDOWN:
			{
				int xPos = GET_X_LPARAM(l); 
				int yPos = GET_Y_LPARAM(l);
				if(pFILE) fprintf(pFILE,"x=%d, y=%d\n", xPos, yPos);
				if(yPos>SPECHEIGHT*0.75)
				{
					//cut frequency
					fftmultiplier[0][xPos]=0.0; //left
					fftmultiplier[1][xPos]=0.0; //right
				}
				else if(yPos>SPECHEIGHT*0.50)
				{
					//attenuate frequency by 50%
					fftmultiplier[0][xPos]=0.5; //left
					fftmultiplier[1][xPos]=0.5; //right
				}
				else
				{
					//amplify frequency by 50%
					fftmultiplier[0][xPos]=1.5; //left
					fftmultiplier[1][xPos]=1.5; //right
				}
			}
			return 0;
		case WM_LBUTTONUP:
			return 0;
		case WM_MOUSEMOVE:
			if(w&MK_LBUTTON)
			{
				int xPos = GET_X_LPARAM(l); 
				int yPos = GET_Y_LPARAM(l);
				if(pFILE) fprintf(pFILE,"x=%d, y=%d\n", xPos, yPos);
				if(yPos>SPECHEIGHT*0.75)
				{
					//cut frequency
					fftmultiplier[0][xPos]=0.0; //left
					fftmultiplier[1][xPos]=0.0; //right
				}
				else if(yPos>SPECHEIGHT*0.50)
				{
					//attenuate frequency by 50%
					fftmultiplier[0][xPos]=0.5; //left
					fftmultiplier[1][xPos]=0.5; //right
				}
				else
				{
					//amplify frequency by 50%
					fftmultiplier[0][xPos]=1.5; //left
					fftmultiplier[1][xPos]=1.5; //right
				}
			}
			return 0;

		case WM_RBUTTONUP:
			//reinit fftmultiplier
			for(int i=0; i<MAX_AUDIODATA; i++ )
			{
				fftmultiplier[0][i]=1.0; //left
				fftmultiplier[1][i]=1.0; //right
			}
			return 0;

		case WM_CREATE:
			win=h;
			//spi, avril 2015, begin
			SetWindowLong(h, GWL_EXSTYLE, GetWindowLong(h, GWL_EXSTYLE) | WS_EX_LAYERED);
			SetLayeredWindowAttributes(h, 0, global_alpha, LWA_ALPHA);
			//SetLayeredWindowAttributes(h, 0, 200, LWA_ALPHA);
			//spi, avril 2015, end
			if(global_fSecondsProcess>0)
			{
				global_timer=timeSetEvent(global_fSecondsProcess*1000,100,(LPTIMECALLBACK)&StopRecording,0,TIME_ONESHOT);
			}
			{ 
				// create bitmap to draw spectrum in (8 bit for easy updating)
				BYTE data[2000]={0};
				BITMAPINFOHEADER *bh=(BITMAPINFOHEADER*)data;
				RGBQUAD *pal=(RGBQUAD*)(data+sizeof(*bh));
				int a;
				bh->biSize=sizeof(*bh);
				bh->biWidth=SPECWIDTH;
				bh->biHeight=SPECHEIGHT; // upside down (line 0=bottom)
				bh->biPlanes=1;
				bh->biBitCount=8;
				bh->biClrUsed=bh->biClrImportant=256;
				// setup palette
				
				if(global_idcolorpalette==0)
				{
					//original palette, green shifting to red
					for (a=1;a<128;a++) {
						pal[a].rgbGreen=256-2*a;
						pal[a].rgbRed=2*a;
					}
					for (a=0;a<32;a++) {
						pal[128+a].rgbBlue=8*a;
						pal[128+32+a].rgbBlue=255;
						pal[128+32+a].rgbRed=8*a;
						pal[128+64+a].rgbRed=255;
						pal[128+64+a].rgbBlue=8*(31-a);
						pal[128+64+a].rgbGreen=8*a;
						pal[128+96+a].rgbRed=255;
						pal[128+96+a].rgbGreen=255;
						pal[128+96+a].rgbBlue=8*a;
					}
				}
				else if(global_idcolorpalette==1)
				{
					//altered palette, red shifting to green
					for (a=1;a<128;a++) {
						pal[a].rgbRed=256-2*a;
						pal[a].rgbGreen=2*a;
					}
					for (a=0;a<32;a++) {
						pal[128+a].rgbBlue=8*a;
						pal[128+32+a].rgbBlue=255;
						pal[128+32+a].rgbGreen=8*a;
						pal[128+64+a].rgbGreen=255;
						pal[128+64+a].rgbBlue=8*(31-a);
						pal[128+64+a].rgbRed=8*a;
						pal[128+96+a].rgbGreen=255;
						pal[128+96+a].rgbRed=255;
						pal[128+96+a].rgbBlue=8*a;
					}
				}
				else if(global_idcolorpalette==2)
				{
					//altered palette, blue shifting to green
					for (a=1;a<128;a++) {
						pal[a].rgbBlue=256-2*a;
						pal[a].rgbGreen=2*a;
					}
					for (a=0;a<32;a++) {
						pal[128+a].rgbBlue=8*a;
						pal[128+32+a].rgbRed=255;
						pal[128+32+a].rgbGreen=8*a;
						pal[128+64+a].rgbGreen=255;
						pal[128+64+a].rgbRed=8*(31-a);
						pal[128+64+a].rgbBlue=8*a;
						pal[128+96+a].rgbGreen=255;
						pal[128+96+a].rgbBlue=255;
						pal[128+96+a].rgbRed=8*a;
					}
				}
				else if(global_idcolorpalette==3)
				{
					//altered palette, black shifting to white - grascale
					for (a=1;a<256;a++) {
						pal[a].rgbRed=a;
						pal[a].rgbBlue=a;
						pal[a].rgbGreen=a;
					}
				}
				else if(global_idcolorpalette==4)
				{
					//altered palette, pink
					for (a=1;a<256;a++) {
						pal[a].rgbRed=255;
						pal[a].rgbBlue=255;
						pal[a].rgbGreen=a;
					}
				}
				else if(global_idcolorpalette==5)
				{
					//altered palette, yellow
					for (a=1;a<256;a++) {
						pal[a].rgbRed=255;
						pal[a].rgbBlue=a;
						pal[a].rgbGreen=255;
					}
				}
				else if(global_idcolorpalette==6)
				{
					//altered palette, cyan
					for (a=1;a<256;a++) {
						pal[a].rgbRed=a;
						pal[a].rgbBlue=255;
						pal[a].rgbGreen=255;
					}
				}
				else if(global_idcolorpalette==7)
				{
					//altered palette, lite green
					for (a=1;a<256;a++) {
						pal[a].rgbRed=a;
						pal[a].rgbBlue=127;
						pal[a].rgbGreen=255;
					}
				}


				// create the bitmap
				specbmp=CreateDIBSection(0,(BITMAPINFO*)bh,DIB_RGB_COLORS,(void**)&specbuf,NULL,0);
				specdc=CreateCompatibleDC(0);
				SelectObject(specdc,specbmp);
			}
			// setup update timer (40hz)
			//timer=timeSetEvent(25,25,(LPTIMECALLBACK)&UpdateSpectrum,0,TIME_PERIODIC);
			break;

		case WM_DESTROY:
			{
				global_abort = true;
				//if (timer) timeKillEvent(timer);
				if (global_timer) timeKillEvent(global_timer);
				//BASS_Free();
				//BASS_RecordFree();
				Sleep(100);
				global_err = Pa_StopStream( global_stream );
				if( global_err != paNoError ) 
				{
					char errorbuf[2048];
					sprintf(errorbuf, "Error stoping stream: %s\n", Pa_GetErrorText(global_err));
					MessageBox(0,errorbuf,0,MB_ICONERROR);
					return 1;
				}
				global_err = Pa_CloseStream( global_stream );
				if( global_err != paNoError ) 
				{
					char errorbuf[2048];
					sprintf(errorbuf, "Error closing stream: %s\n", Pa_GetErrorText(global_err));
					MessageBox(0,errorbuf,0,MB_ICONERROR);
					return 1;
				}
				Pa_Terminate();
				if (specdc) DeleteDC(specdc);
				if (specbmp) DeleteObject(specbmp);

				delete[] audioData[0];
				delete[] audioData[1];
				delete[] fftaudioData[0];
				delete[] fftaudioData[1];
				delete[] fftmultiplier[0];
				delete[] fftmultiplier[1];
				if(pFILE) fclose(pFILE);
				pFILE=NULL;

				int nShowCmd = false;
				//ShellExecuteA(NULL, "open", "end.bat", "", NULL, nShowCmd);
				//ShellExecuteA(NULL, "open", "end.ahk", "", NULL, nShowCmd);
				ShellExecuteA(NULL, "open", global_end.c_str(), "", NULL, nShowCmd);
				
				PostQuitMessage(0);
			}
			break;
	}
	return DefWindowProc(h, m, w, l);
}

PCHAR*
    CommandLineToArgvA(
        PCHAR CmdLine,
        int* _argc
        )
    {
        PCHAR* argv;
        PCHAR  _argv;
        ULONG   len;
        ULONG   argc;
        CHAR   a;
        ULONG   i, j;

        BOOLEAN  in_QM;
        BOOLEAN  in_TEXT;
        BOOLEAN  in_SPACE;

        len = strlen(CmdLine);
        i = ((len+2)/2)*sizeof(PVOID) + sizeof(PVOID);

        argv = (PCHAR*)GlobalAlloc(GMEM_FIXED,
            i + (len+2)*sizeof(CHAR));

        _argv = (PCHAR)(((PUCHAR)argv)+i);

        argc = 0;
        argv[argc] = _argv;
        in_QM = FALSE;
        in_TEXT = FALSE;
        in_SPACE = TRUE;
        i = 0;
        j = 0;

        while( a = CmdLine[i] ) {
            if(in_QM) {
                if(a == '\"') {
                    in_QM = FALSE;
                } else {
                    _argv[j] = a;
                    j++;
                }
            } else {
                switch(a) {
                case '\"':
                    in_QM = TRUE;
                    in_TEXT = TRUE;
                    if(in_SPACE) {
                        argv[argc] = _argv+j;
                        argc++;
                    }
                    in_SPACE = FALSE;
                    break;
                case ' ':
                case '\t':
                case '\n':
                case '\r':
                    if(in_TEXT) {
                        _argv[j] = '\0';
                        j++;
                    }
                    in_TEXT = FALSE;
                    in_SPACE = TRUE;
                    break;
                default:
                    in_TEXT = TRUE;
                    if(in_SPACE) {
                        argv[argc] = _argv+j;
                        argc++;
                    }
                    _argv[j] = a;
                    j++;
                    in_SPACE = FALSE;
                    break;
                }
            }
            i++;
        }
        _argv[j] = '\0';
        argv[argc] = NULL;

        (*_argc) = argc;
        return argv;
    }

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,LPSTR lpCmdLine, int nCmdShow)
{
	int nShowCmd = false;

	//LPWSTR *szArgList;
	LPSTR *szArgList;
	int argCount;
	//szArgList = CommandLineToArgvW(GetCommandLineW(), &argCount);
	szArgList = CommandLineToArgvA(GetCommandLine(), &argCount);
	if (szArgList == NULL)
	{
		MessageBox(NULL, "Unable to parse command line", "Error", MB_OK);
		return 10;
	}
	global_audioinputdevicename="E-MU ASIO"; //"Wave (2- E-MU E-DSP Audio Proce"
	if(argCount>1)
	{
		//global_filename = szArgList[1]; 
		global_audioinputdevicename = szArgList[1]; 
	
		/*
		int ret = wcstombs ( global_buffer, szArgList[1], sizeof(global_buffer) );
		if (ret==sizeof(global_buffer)) global_buffer[sizeof(global_buffer)-1]='\0';
		global_filename = global_buffer; 
		*/
	}
	global_inputAudioChannelSelectors[0] = 0; // on emu patchmix ASIO device channel 1 (left)
	global_inputAudioChannelSelectors[1] = 1; // on emu patchmix ASIO device channel 2 (right)
	//global_inputAudioChannelSelectors[0] = 2; // on emu patchmix ASIO device channel 3 (left)
	//global_inputAudioChannelSelectors[1] = 3; // on emu patchmix ASIO device channel 4 (right)
	//global_inputAudioChannelSelectors[0] = 8; // on emu patchmix ASIO device channel 9 (left)
	//global_inputAudioChannelSelectors[1] = 9; // on emu patchmix ASIO device channel 10 (right)
	//global_inputAudioChannelSelectors[0] = 10; // on emu patchmix ASIO device channel 11 (left)
	//global_inputAudioChannelSelectors[1] = 11; // on emu patchmix ASIO device channel 12 (right)
	if(argCount>2)
	{
		global_inputAudioChannelSelectors[0]=atoi((LPCSTR)(szArgList[2])); //0 for first asio channel (left) or 2, 4, 6, etc.
	}
	if(argCount>3)
	{
		global_inputAudioChannelSelectors[1]=atoi((LPCSTR)(szArgList[3])); //1 for second asio channel (right) or 3, 5, 7, etc.
	}

	global_audiooutputdevicename="E-MU ASIO"; //"Wave (2- E-MU E-DSP Audio Proce"
	if(argCount>4)
	{
		//global_filename = szArgList[1];
		global_audiooutputdevicename = szArgList[4]; 
	}
	global_outputAudioChannelSelectors[0] = 0; // on emu patchmix ASIO device channel 1 (left)
	global_outputAudioChannelSelectors[1] = 1; // on emu patchmix ASIO device channel 2 (right)
	//global_outputAudioChannelSelectors[0] = 2; // on emu patchmix ASIO device channel 3 (left)
	//global_outputAudioChannelSelectors[1] = 3; // on emu patchmix ASIO device channel 4 (right)
	//global_outputAudioChannelSelectors[0] = 8; // on emu patchmix ASIO device channel 9 (left)
	//global_outputAudioChannelSelectors[1] = 9; // on emu patchmix ASIO device channel 10 (right)
	//global_outputAudioChannelSelectors[0] = 10; // on emu patchmix ASIO device channel 11 (left)
	//global_outputAudioChannelSelectors[1] = 11; // on emu patchmix ASIO device channel 12 (right)
	if(argCount>5)
	{
		global_outputAudioChannelSelectors[0]=atoi((LPCSTR)(szArgList[5])); //0 for first asio channel (left) or 2, 4, 6, etc.
	}
	if(argCount>6)
	{
		global_outputAudioChannelSelectors[1]=atoi((LPCSTR)(szArgList[6])); //1 for second asio channel (right) or 3, 5, 7, etc.
	}


	global_fSecondsProcess = -1.0; //negative for always processing
	if(argCount>7)
	{
		global_fSecondsProcess = atof((LPCSTR)(szArgList[7]));
	}
	if(argCount>8)
	{
		global_x = atoi((LPCSTR)(szArgList[8]));
	}
	if(argCount>9)
	{
		global_y = atoi((LPCSTR)(szArgList[9]));
	}
	if(argCount>710)
	{
		specmode = atoi((LPCSTR)(szArgList[10]));
	}
	if(argCount>11)
	{
		global_classname = szArgList[11]; 
	}
	if(argCount>12)
	{
		global_title = szArgList[12]; 
	}
	if(argCount>13)
	{
		global_begin = szArgList[13]; 
	}
	if(argCount>14)
	{
		global_end = szArgList[14]; 
	}
	if(argCount>15)
	{
		global_idcolorpalette = atoi((LPCSTR)(szArgList[15]));
	}
	if(argCount>16)
	{
		global_bands = atoi((LPCSTR)(szArgList[16]));
	}
	if(argCount>17)
	{
		//SPECWIDTH = atoi((LPCSTR)(szArgList[17]));
	}
	if(argCount>18)
	{
		//SPECHEIGHT = atoi((LPCSTR)(szArgList[18]));
	}
	if(argCount>19)
	{
		global_alpha = atoi(szArgList[19]);
	}
	LocalFree(szArgList);


	//ShellExecuteA(NULL, "open", "begin.bat", "", NULL, nShowCmd);
	//ShellExecuteA(NULL, "open", "begin.ahk", "", NULL, nShowCmd);
	ShellExecuteA(NULL, "open", global_begin.c_str(), "", NULL, nShowCmd);

	//////////////////////////
	//initialize random number
	//////////////////////////
	srand((unsigned)time(0));

	///////////////////////
	//initialize port audio
	///////////////////////
    global_err = Pa_Initialize();
    if( global_err != paNoError )
	{
		MessageBox(0,"portaudio initialization failed",0,MB_ICONERROR);
		return 1;
	}

	////////////////////////
	//audio device selection
	////////////////////////
	pFILE = fopen("devices.txt","w");
	SelectAudioInputDevice();
	SelectAudioOutputDevice();
	if(pFILE) fclose(pFILE);
	pFILE=NULL;

	//////////////////////
	//init spectrum filter
	//////////////////////
	audioData[0] = new float[MAX_AUDIODATA]; //usually, buffer is much smaller than MAX_AUDIODATA
	audioData[1] = new float[MAX_AUDIODATA];
	fftaudioData[0] = new float[MAX_AUDIODATA]; //usually, buffer is much smaller than MAX_AUDIODATA
	fftaudioData[1] = new float[MAX_AUDIODATA];
	fftmultiplier[0] = new float[MAX_AUDIODATA];
	fftmultiplier[1] = new float[MAX_AUDIODATA];
	//init fftmultiplier
	for(int i=0; i<MAX_AUDIODATA; i++ )
    {
		fftmultiplier[0][i]=1.0; //left
		fftmultiplier[1][i]=1.0; //right
    }

	//////////////
    //setup stream  
	//////////////
    global_err = Pa_OpenStream(
        &global_stream,
        &global_inputParameters,
        &global_outputParameters,
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        0, //paClipOff,      // we won't output out of range samples so don't bother clipping them
        spectrumCallback, 
        NULL ); //no callback userData
    if( global_err != paNoError ) 
	{
		char errorbuf[2048];
        sprintf(errorbuf, "Unable to open stream: %s\n", Pa_GetErrorText(global_err));
		MessageBox(0,errorbuf,0,MB_ICONERROR);
		//if(pFILE) fprintf(pFILE, "%s\n", errorbuf);
		//fclose(pFILE);pFILE=NULL;
        return 1;
    }

	//////////////
    //start stream  
	//////////////
    global_err = Pa_StartStream( global_stream );
    if( global_err != paNoError ) 
	{
		char errorbuf[2048];
        sprintf(errorbuf, "Unable to start stream: %s\n", Pa_GetErrorText(global_err));
		MessageBox(0,errorbuf,0,MB_ICONERROR);
        return 1;
    }


	WNDCLASS wc={0};
    MSG msg;

	/*
	// check the correct BASS was loaded
	if (HIWORD(BASS_GetVersion())!=BASSVERSION) {
		MessageBox(0,"An incorrect version of BASS.DLL was loaded",0,MB_ICONERROR);
		return 0;
	}
	*/

	// register window class and create the window
	wc.lpfnWndProc = SpectrumWindowProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1)); //spi, added
	wc.lpszClassName = global_classname.c_str();
	if (!RegisterClass(&wc) || !CreateWindow(global_classname.c_str(),
			//"BASS spectrum example (click to toggle mode)",
			//"spispectrumplay (click to toggle mode)",
			global_title.c_str(),
			//WS_POPUPWINDOW|WS_VISIBLE, global_x, global_y,

			WS_POPUP|WS_VISIBLE, global_x, global_y,
			
			//WS_POPUPWINDOW|WS_CAPTION|WS_VISIBLE, global_x, global_y,
			//WS_POPUPWINDOW|WS_VISIBLE, 200, 200,
			SPECWIDTH,
			//SPECWIDTH+2*GetSystemMetrics(SM_CXDLGFRAME),
			SPECHEIGHT,
			//SPECHEIGHT+GetSystemMetrics(SM_CYCAPTION)+2*GetSystemMetrics(SM_CYDLGFRAME),
			NULL, NULL, hInstance, NULL)) 
	{
		//Error("Can't create window");
		MessageBox(0,"Can't create window",0,MB_ICONERROR);
		return 0;
	}
	ShowWindow(win, SW_SHOWNORMAL);

	while (GetMessage(&msg,NULL,0,0)>0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}
