
/*
 *  - load portaudio.dll
 *  - output device n to stdout
 *
 *  win:
 *  gcc -DWIN_DLL pa-stdout.c
 *  (cl /DWIN_DLL /DCYGWIN pa-stdout.c)
 *
 *  linux:
 *  gcc pa-stdout.c -lportaudio
 *
 *  [select SDR Audio Output channel]
 *  ./pa-stdout.exe --list
 *  ./pa-stdout.exe [devNo] | ./rs41dm_dft.exe --ecc2 --crc -vx --ptu
 */

#include <stdio.h>
#include <stdlib.h>

#ifdef WIN_DLL
    #include <windows.h>
#endif

#ifdef CYGWIN
  #include <fcntl.h>  // cygwin: _setmode()
  #include <io.h>
#endif


#ifndef WIN_DLL
    #include "portaudio.h"
#else
//
//------------------------------------------------------------------------------------------------------

#define paNoDevice       ((PaDeviceIndex)-1)

#define paClipOff        ((PaStreamFlags)  0x00000001)

#define paFloat32        ((PaSampleFormat) 0x00000001) /**< @see PaSampleFormat */
#define paInt32          ((PaSampleFormat) 0x00000002) /**< @see PaSampleFormat */
#define paInt24          ((PaSampleFormat) 0x00000004) /**< Packed 24 bit format. @see PaSampleFormat */
#define paInt16          ((PaSampleFormat) 0x00000008) /**< @see PaSampleFormat */
#define paInt8           ((PaSampleFormat) 0x00000010) /**< @see PaSampleFormat */
#define paUInt8          ((PaSampleFormat) 0x00000020) /**< @see PaSampleFormat */
#define paCustomFormat   ((PaSampleFormat) 0x00010000) /**< @see PaSampleFormat */


typedef int PaError;
typedef enum PaErrorCode
{
    paNoError = 0,
    paNotInitialized = -10000,
    paUnanticipatedHostError,
    paInvalidChannelCount,
    paInvalidSampleRate,
    paInvalidDevice,
    paInvalidFlag,
    paSampleFormatNotSupported,
    paBadIODeviceCombination,
    paInsufficientMemory,
    paBufferTooBig,
    paBufferTooSmall,
    paNullCallback,
    paBadStreamPtr,
    paTimedOut,
    paInternalError,
    paDeviceUnavailable,
    paIncompatibleHostApiSpecificStreamInfo,
    paStreamIsStopped,
    paStreamIsNotStopped,
    paInputOverflowed,
    paOutputUnderflowed,
    paHostApiNotFound,
    paInvalidHostApi,
    paCanNotReadFromACallbackStream,
    paCanNotWriteToACallbackStream,
    paCanNotReadFromAnOutputOnlyStream,
    paCanNotWriteToAnInputOnlyStream,
    paIncompatibleStreamHostApi,
    paBadBufferPtr
} PaErrorCode;

typedef int PaDeviceIndex;
typedef int PaHostApiIndex;
typedef double PaTime;

typedef struct PaDeviceInfo
{
    int structVersion;  /* this is struct version 2 */
    const char *name;
    PaHostApiIndex hostApi; /**< note this is a host API index, not a type id*/

    int maxInputChannels;
    int maxOutputChannels;

    /** Default latency values for interactive performance. */
    PaTime defaultLowInputLatency;
    PaTime defaultLowOutputLatency;
    /** Default latency values for robust non-interactive applications (eg. playing sound files). */
    PaTime defaultHighInputLatency;
    PaTime defaultHighOutputLatency;

    double defaultSampleRate;
} PaDeviceInfo;


typedef unsigned long PaStreamFlags;
typedef unsigned long PaSampleFormat;

typedef struct PaStreamCallbackTimeInfo{
    PaTime inputBufferAdcTime;  /**< The time when the first sample of the input buffer was captured at the ADC input */
    PaTime currentTime;         /**< The time when the stream callback was invoked */
    PaTime outputBufferDacTime; /**< The time when the first sample of the output buffer will output the DAC */
} PaStreamCallbackTimeInfo;

typedef unsigned long PaStreamCallbackFlags;


typedef struct PaStreamParameters
{
    PaDeviceIndex device;
    int channelCount;
    PaSampleFormat sampleFormat;
    PaTime suggestedLatency;
    void *hostApiSpecificStreamInfo;

} PaStreamParameters;

typedef void PaStream;

typedef int PaStreamCallback(
    const void *input, void *output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData );

//
//------------------------------------------------------------------------------------------------------

// load WIN-DLL

// prototypes ( __cdecl -> WINAPIV* , __stdcall -> WINAPI*)

typedef int (WINAPI* t_Pa_Initialize)(void);
typedef int (WINAPI* t_Pa_GetDefaultInputDevice)(void);
typedef int (WINAPI* t_Pa_GetDeviceCount)(void);

typedef PaDeviceInfo* (WINAPI* t_Pa_GetDeviceInfo)(PaDeviceIndex device);

t_Pa_Initialize             Pa_Initialize;
t_Pa_GetDefaultInputDevice  Pa_GetDefaultInputDevice;
t_Pa_GetDeviceCount         Pa_GetDeviceCount;
t_Pa_GetDeviceInfo          Pa_GetDeviceInfo;


typedef int   (WINAPI* t_Pa_OpenStream)(PaStream**,PaStreamParameters*,PaStreamParameters*,double,
                                        unsigned long,PaStreamFlags,PaStreamCallback*,void*);
typedef int   (WINAPI* t_Pa_StartStream)(PaStream *);
typedef int   (WINAPI* t_Pa_ReadStream)(PaStream*,void *,unsigned long);
typedef int   (WINAPI* t_Pa_CloseStream)(PaStream*);
typedef int   (WINAPI* t_Pa_Terminate)(void);
typedef char* (WINAPI* t_Pa_GetErrorText)(PaError);

t_Pa_OpenStream     Pa_OpenStream;
t_Pa_StartStream    Pa_StartStream;
t_Pa_ReadStream     Pa_ReadStream;
t_Pa_CloseStream    Pa_CloseStream;
t_Pa_Terminate      Pa_Terminate;
t_Pa_GetErrorText   Pa_GetErrorText;


HINSTANCE  pa_handle;


int adr_functions() {

    int ret = 0;

    Pa_Initialize = (t_Pa_Initialize)GetProcAddress(pa_handle, "Pa_Initialize");
    if (Pa_Initialize == NULL) {
        fprintf(stderr, "ERROR: GetProcAddress\n");
        ret = -2;
    }

    Pa_GetDefaultInputDevice = (t_Pa_GetDefaultInputDevice)GetProcAddress(pa_handle, "Pa_GetDefaultInputDevice");
    if (Pa_GetDefaultInputDevice == NULL) {
        fprintf(stderr, "ERROR: GetProcAddress\n");
        ret = -2;
    }

    Pa_GetDeviceCount = (t_Pa_GetDeviceCount)GetProcAddress(pa_handle, "Pa_GetDeviceCount");
    if (Pa_GetDeviceCount == NULL) {
        fprintf(stderr, "ERROR: GetProcAddress\n");
        ret = -2;
    }

    Pa_GetDeviceInfo = (t_Pa_GetDeviceInfo)GetProcAddress(pa_handle, "Pa_GetDeviceInfo");
    if (Pa_GetDeviceInfo == NULL) {
        fprintf(stderr, "ERROR: GetProcAddress\n");
        ret = -2;
    }

    Pa_OpenStream = (t_Pa_OpenStream)GetProcAddress(pa_handle, "Pa_OpenStream");
    if (Pa_OpenStream == NULL) {
        fprintf(stderr, "ERROR: GetProcAddress\n");
        ret = -2;
    }

    Pa_StartStream = (t_Pa_StartStream)GetProcAddress(pa_handle, "Pa_StartStream");
    if (Pa_StartStream == NULL) {
        fprintf(stderr, "ERROR: GetProcAddress\n");
        ret = -2;
    }

    Pa_ReadStream = (t_Pa_ReadStream)GetProcAddress(pa_handle, "Pa_ReadStream");
    if (Pa_ReadStream == NULL) {
        fprintf(stderr, "ERROR: GetProcAddress\n");
        ret = -2;
    }

    Pa_CloseStream = (t_Pa_CloseStream)GetProcAddress(pa_handle, "Pa_CloseStream");
    if (Pa_CloseStream == NULL) {
        fprintf(stderr, "ERROR: GetProcAddress\n");
        ret = -2;
    }

    Pa_Terminate = (t_Pa_Terminate)GetProcAddress(pa_handle, "Pa_Terminate");
    if (Pa_Terminate == NULL) {
        fprintf(stderr, "ERROR: GetProcAddress\n");
        ret = -2;
    }

    Pa_GetErrorText = (t_Pa_GetErrorText)GetProcAddress(pa_handle, "Pa_GetErrorText");
    if (Pa_GetErrorText == NULL) {
        fprintf(stderr, "ERROR: GetProcAddress\n");
        ret = -2;
    }

    return ret;
}
#endif


#define SAMPLE_RATE       (48000)
#define FRAMES_PER_BUFFER (1024)
#define N_CH              (0x02)

// 16-bit int samples
#define PA_SAMPLE_TYPE  paInt16
typedef short SAMPLE; // 16-bit


int get_Devices(int list) {
    int i;
    PaDeviceIndex numDevices = Pa_GetDeviceCount();

    fprintf(stderr, "#devices: %d\n", numDevices);

    for (i = 0; i < numDevices; i++)
    {
        PaDeviceInfo *devInfo = Pa_GetDeviceInfo(i);
        if (list) fprintf(stderr, "[%2d]  %s\n", i, devInfo->name);
    }

    return numDevices;
}


unsigned char wav_hdr[] = {                  // 0x80, 0xbb, 0x00, 0x00: sample_rate=48000
0x52, 0x49, 0x46, 0x46, 0x24, 0xf0, 0xff, 0x7f, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6d, 0x74, 0x20,
0x10, 0x00, 0x00, 0x00, 0x01, 0x00, N_CH, 0x00, 0x80, 0xbb, 0x00, 0x00, 0x00, 0xee, 0x02, 0x00,
0x04, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0xf0, 0xff, 0x7f};
//0x10,0x00: bits/sample=16      // 0x0n, 0x00: channels=n


int main(int argc, char* argv[])
{
    int ret = 0;

    int devNo = -1,
        numDevices = 0,
        list = 0;

    PaError             err = paNoError;
    PaStreamParameters  inputParameters;
    PaStream           *stream;
    PaDeviceInfo       *devInfo = NULL;

    SAMPLE  *dataSamples = NULL;
    int      totalFrames;
    int      numSamples;


#ifdef CYGWIN
    _setmode(fileno(stdout), _O_BINARY);  // _setmode(_fileno(stdout), _O_BINARY);
#endif
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);


#ifdef WIN_DLL
    pa_handle = LoadLibrary("PortAudio.dll");  // x86/x64 (same as gcc/cl)

    if (pa_handle == NULL) {
        fprintf(stderr, "ERROR: loadlibrary\n");
        return -1;
    }

    ret = adr_functions();
    if (ret != 0) goto error;
#endif


    err = Pa_Initialize();
    if ( err != paNoError ) goto done;


    if (argv[1]) {
        if (strcmp(argv[1], "--list") == 0) list = 1;
        devNo = atoi(argv[1]);
        if (devNo == 0 && argv[1][0] != '0') devNo = -1;
    }

    numDevices = get_Devices(list);
    if ( list ) goto done;

    if ( devNo >= numDevices ) devNo = -1;


    totalFrames = FRAMES_PER_BUFFER;
    numSamples = totalFrames * N_CH;
    dataSamples = (SAMPLE *) calloc( numSamples, sizeof(SAMPLE) );
    if( dataSamples == NULL )
    {
        fprintf(stderr, "ERROR: malloc\n");
        goto done;
    }


    if (devNo < 0) inputParameters.device = Pa_GetDefaultInputDevice();
    else           inputParameters.device = devNo;

    fprintf(stderr, "Input Device:\n");
    devInfo = Pa_GetDeviceInfo(inputParameters.device);
    fprintf(stderr, "[%d]  %s\n", inputParameters.device, devInfo->name);

    if (inputParameters.device == paNoDevice) {
        fprintf(stderr,"Error: No default input device.\n");
        goto done;
    }

    inputParameters.channelCount = N_CH;
    inputParameters.sampleFormat = PA_SAMPLE_TYPE;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = NULL;


    err = Pa_OpenStream(
              &stream,
              &inputParameters,
              NULL,             // &outputParameters,
              SAMPLE_RATE,
              FRAMES_PER_BUFFER,
              paClipOff,
              NULL,
              NULL );
    if ( err != paNoError ) goto done;

    err = Pa_StartStream( stream );
    if ( err != paNoError ) goto done;

    fwrite( wav_hdr, 1, sizeof(wav_hdr)/*44*/, stdout );
    while (1) {
        err = Pa_ReadStream( stream, dataSamples, totalFrames );
        if( err != paNoError ) goto done;
        fwrite( dataSamples, N_CH * sizeof(SAMPLE), totalFrames, stdout );
    }

    err = Pa_CloseStream( stream );
    if( err != paNoError ) goto done;


done:
    Pa_Terminate();
    if ( dataSamples ) free(dataSamples);
    if ( err != paNoError )
    {
        fprintf( stderr, "Error number: %d\n", err );
        fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
        err = 1;          /* Always return 0 or 1, but no other return codes. */
    }

error:
#ifdef WIN_DLL
    FreeLibrary(pa_handle);
#endif
    if (ret) err = ret;

    return err;
}

