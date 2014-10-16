/*
 * DISTRHO Plugin Framework (DPF)
 * Copyright (C) 2012-2014 Filipe Coelho <falktx@falktx.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "DistrhoPluginInternal.hpp"

#if DISTRHO_PLUGIN_HAS_UI
# include "DistrhoUIInternal.hpp"
#endif

#ifndef __cdecl
# define __cdecl
#endif

// has some conflicts
#ifdef noexcept
# undef noexcept
#endif

#define VESTIGE_HEADER
#define VST_FORCE_DEPRECATED 0

#include <map>
#include <string>

#ifdef VESTIGE_HEADER
# include "vestige/aeffectx.h"
#define effFlagsProgramChunks (1 << 5)
#define effSetProgramName 4
#define effGetParamLabel 6
#define effGetParamDisplay 7
#define effGetChunk 23
#define effSetChunk 24
#define effCanBeAutomated 26
#define effGetProgramNameIndexed 29
#define effGetPlugCategory 35
#define effIdle 53
#define kPlugCategEffect 1
#define kPlugCategSynth 2
#define kVstVersion 2400
struct ERect {
    int16_t top, left, bottom, right;
};
#else
# include "vst/aeffectx.h"
#endif

START_NAMESPACE_DISTRHO

typedef std::map<const d_string,d_string> StringMap;

// -----------------------------------------------------------------------

void strncpy(char* const dst, const char* const src, const size_t size)
{
    std::strncpy(dst, src, size-1);
    dst[size-1] = '\0';
}

void snprintf_param(char* const dst, const float value, const size_t size)
{
    std::snprintf(dst, size-1, "%f", value);
    dst[size-1] = '\0';
}

#if DISTRHO_PLUGIN_HAS_UI
// -----------------------------------------------------------------------

class UiHelper
{
public:
    UiHelper()
        : parameterChecks(nullptr),
          parameterValues(nullptr) {}

    virtual ~UiHelper()
    {
        if (parameterChecks != nullptr)
        {
            delete[] parameterChecks;
            parameterChecks = nullptr;
        }
        if (parameterValues != nullptr)
        {
            delete[] parameterValues;
            parameterValues = nullptr;
        }
    }

    bool*   parameterChecks;
    float*  parameterValues;

#if DISTRHO_PLUGIN_WANT_STATE
    virtual void setStateFromUI(const char* const newKey, const char* const newValue) = 0;
#endif
};

// -----------------------------------------------------------------------

class UIVst
{
public:
    UIVst(const audioMasterCallback audioMaster, AEffect* const effect, UiHelper* const uiHelper, PluginExporter* const plugin, const intptr_t winId)
        : fAudioMaster(audioMaster),
          fEffect(effect),
          fUiHelper(uiHelper),
          fPlugin(plugin),
          fUI(this, winId, editParameterCallback, setParameterCallback, setStateCallback, sendNoteCallback, setSizeCallback, plugin->getInstancePointer())
    {
    }

    // -------------------------------------------------------------------

    void idle()
    {
        for (uint32_t i=0, count = fPlugin->getParameterCount(); i < count; ++i)
        {
            if (fUiHelper->parameterChecks[i])
            {
                fUiHelper->parameterChecks[i] = false;
                fUI.parameterChanged(i, fUiHelper->parameterValues[i]);
            }
        }

        fUI.idle();
    }

    int16_t getWidth() const
    {
        return fUI.getWidth();
    }

    int16_t getHeight() const
    {
        return fUI.getHeight();
    }

    void setSampleRate(const double newSampleRate)
    {
        fUI.setSampleRate(newSampleRate, true);
    }

    // -------------------------------------------------------------------
    // functions called from the plugin side, may block

#if DISTRHO_PLUGIN_WANT_STATE
    void setStateFromPlugin(const char* const key, const char* const value)
    {
        fUI.stateChanged(key, value);
    }
#endif

    // -------------------------------------------------------------------

protected:
    intptr_t hostCallback(const int32_t opcode, const int32_t index, const intptr_t value, void* const ptr, const float opt)
    {
        return fAudioMaster(fEffect, opcode, index, value, ptr, opt);
    }

    void editParameter(const uint32_t index, const bool started)
    {
        hostCallback(started ? audioMasterBeginEdit : audioMasterEndEdit, index, 0, nullptr, 0.0f);
    }

    void setParameterValue(const uint32_t index, const float realValue)
    {
        const ParameterRanges& ranges(fPlugin->getParameterRanges(index));
        const float perValue(ranges.getNormalizedValue(realValue));

        fPlugin->setParameterValue(index, realValue);
        hostCallback(audioMasterAutomate, index, 0, nullptr, perValue);
    }

    void setState(const char* const key, const char* const value)
    {
#if DISTRHO_PLUGIN_WANT_STATE
        fUiHelper->setStateFromUI(key, value);
#else
        return; // unused
        (void)key;
        (void)value;
#endif
    }

    void sendNote(const uint8_t channel, const uint8_t note, const uint8_t velocity)
    {
#if 0 //DISTRHO_PLUGIN_HAS_MIDI_INPUT
        // TODO
#else
        return; // unused
        (void)channel;
        (void)note;
        (void)velocity;
#endif
    }

    void setSize(const uint width, const uint height)
    {
        fUI.setWindowSize(width, height);
        hostCallback(audioMasterSizeWindow, width, height, nullptr, 0.0f);
    }

private:
    // Vst stuff
    const audioMasterCallback fAudioMaster;
    AEffect* const fEffect;
    UiHelper* const fUiHelper;
    PluginExporter* const fPlugin;

    // Plugin UI
    UIExporter fUI;

    // -------------------------------------------------------------------
    // Callbacks

    #define handlePtr ((UIVst*)ptr)

    static void editParameterCallback(void* ptr, uint32_t index, bool started)
    {
        handlePtr->editParameter(index, started);
    }

    static void setParameterCallback(void* ptr, uint32_t rindex, float value)
    {
        handlePtr->setParameterValue(rindex, value);
    }

    static void setStateCallback(void* ptr, const char* key, const char* value)
    {
        handlePtr->setState(key, value);
    }

    static void sendNoteCallback(void* ptr, uint8_t channel, uint8_t note, uint8_t velocity)
    {
        handlePtr->sendNote(channel, note, velocity);
    }

    static void setSizeCallback(void* ptr, uint width, uint height)
    {
        handlePtr->setSize(width, height);
    }

    #undef handlePtr
};
#endif

// -----------------------------------------------------------------------

#if DISTRHO_PLUGIN_HAS_UI
class PluginVst : public UiHelper
#else
class PluginVst
#endif
{
public:
    PluginVst(const audioMasterCallback audioMaster, AEffect* const effect)
        : fAudioMaster(audioMaster),
          fEffect(effect)
    {
        std::memset(fProgramName, 0, sizeof(char)*(64+1));
        std::strcpy(fProgramName, "Default");

#if DISTRHO_PLUGIN_HAS_MIDI_INPUT
        fMidiEventCount = 0;
#endif

#if DISTRHO_PLUGIN_HAS_UI
        fVstUI          = nullptr;
        fVstRect.top    = 0;
        fVstRect.left   = 0;
        fVstRect.bottom = 0;
        fVstRect.right  = 0;

        if (const uint32_t paramCount = fPlugin.getParameterCount())
        {
            parameterChecks = new bool[paramCount];
            parameterValues = new float[paramCount];

            for (uint32_t i=0; i < paramCount; ++i)
            {
                parameterChecks[i] = false;
                parameterValues[i] = 0.0f;
            }
        }
# if DISTRHO_OS_MAC
#  ifdef __LP64__
        fUsingNsView = true;
#  else
#  warning 32bit VST UIs on OSX only work if the host supports "hasCockosViewAsConfig"
        fUsingNsView = false;
#  endif
# endif // DISTRHO_OS_MAC
#endif // DISTRHO_PLUGIN_HAS_UI

#if DISTRHO_PLUGIN_WANT_STATE
        fStateChunk = nullptr;

        for (uint32_t i=0, count=fPlugin.getStateCount(); i<count; ++i)
        {
            const d_string& d_key(fPlugin.getStateKey(i));
            fStateMap[d_key] = fPlugin.getStateDefaultValue(i);
        }
#endif
    }

    ~PluginVst()
    {
#if DISTRHO_PLUGIN_WANT_STATE
        if (fStateChunk != nullptr)
        {
            delete[] fStateChunk;
            fStateChunk = nullptr;
        }

        fStateMap.clear();
#endif
    }

    intptr_t vst_dispatcher(const int32_t opcode, const int32_t index, const intptr_t value, void* const ptr, const float opt)
    {
        switch (opcode)
        {
        case effGetProgram:
            return 0;

        case effSetProgramName:
            if (char* const programName = (char*)ptr)
            {
                DISTRHO::strncpy(fProgramName, programName, 64);
                return 1;
            }
            break;

        case effGetProgramName:
            if (char* const programName = (char*)ptr)
            {
                DISTRHO::strncpy(programName, fProgramName, 24);
                return 1;
            }
            break;

        case effGetProgramNameIndexed:
            if (char* const programName = (char*)ptr)
            {
                DISTRHO::strncpy(programName, fProgramName, 24);
                return 1;
            }
            break;

        case effGetParamDisplay:
            if (ptr != nullptr && index < static_cast<int32_t>(fPlugin.getParameterCount()))
            {
                DISTRHO::snprintf_param((char*)ptr, fPlugin.getParameterValue(index), 24);
                return 1;
            }
            break;

        case effSetSampleRate:
            fPlugin.setSampleRate(opt, true);

            if (fVstUI != nullptr)
                fVstUI->setSampleRate(opt);
            break;

        case effSetBlockSize:
            fPlugin.setBufferSize(value, true);
            break;

        case effMainsChanged:
            if (value != 0)
            {
                fPlugin.activate();
#if DISTRHO_PLUGIN_HAS_MIDI_INPUT
                fMidiEventCount = 0;
#endif
            }
            else
            {
                fPlugin.deactivate();
            }
            break;

#if DISTRHO_PLUGIN_HAS_UI
        case effEditGetRect:
            if (fVstUI != nullptr)
            {
                fVstRect.right  = fVstUI->getWidth();
                fVstRect.bottom = fVstUI->getHeight();
            }
            else
            {
                d_lastUiSampleRate = fPlugin.getSampleRate();

                UIExporter tmpUI(nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr, fPlugin.getInstancePointer());
                fVstRect.right  = tmpUI.getWidth();
                fVstRect.bottom = tmpUI.getHeight();
                tmpUI.quit();
            }
            *(ERect**)ptr = &fVstRect;
            return 1;

        case effEditOpen:
            if (fVstUI == nullptr)
            {
# if DISTRHO_OS_MAC
                if (! fUsingNsView)
                {
                    d_stderr("Host doesn't support hasCockosViewAsConfig, cannot use UI");
                    return 0;
                }
# endif
                d_lastUiSampleRate = fPlugin.getSampleRate();

                fVstUI = new UIVst(fAudioMaster, fEffect, this, &fPlugin, (intptr_t)ptr);

# if DISTRHO_PLUGIN_WANT_STATE
                for (StringMap::const_iterator cit=fStateMap.begin(), cite=fStateMap.end(); cit != cite; ++cit)
                {
                    const d_string& key   = cit->first;
                    const d_string& value = cit->second;

                    fVstUI->setStateFromPlugin(key, value);
                }
# endif
                for (uint32_t i=0, count=fPlugin.getParameterCount(); i < count; ++i)
                    setParameterValueFromPlugin(i, fPlugin.getParameterValue(i));

                fVstUI->idle();
                return 1;
            }
            break;

        case effEditClose:
            if (fVstUI != nullptr)
            {
                delete fVstUI;
                fVstUI = nullptr;
                return 1;
            }
            break;

        //case effIdle:
        case effEditIdle:
            if (fVstUI != nullptr)
                fVstUI->idle();
            break;
#endif // DISTRHO_PLUGIN_HAS_UI

#if DISTRHO_PLUGIN_WANT_STATE
        case effGetChunk:
            if (ptr == nullptr)
                return 0;

            if (fStateChunk != nullptr)
            {
                delete[] fStateChunk;
                fStateChunk = nullptr;
            }

            if (fPlugin.getStateCount() == 0)
            {
                fStateChunk    = new char[1];
                fStateChunk[0] = '\0';
                return 1;
            }
            else
            {
                d_string chunkStr;

                for (StringMap::const_iterator cit=fStateMap.begin(), cite=fStateMap.end(); cit != cite; ++cit)
                {
                    const d_string& key   = cit->first;
                    const d_string& value = cit->second;

                    // join key and value
                    d_string tmpStr;
                    tmpStr  = key;
                    tmpStr += "\xff";
                    tmpStr += value;
                    tmpStr += "\xff";

                    chunkStr += tmpStr;
                }

                const std::size_t chunkSize(chunkStr.length()+1);

                fStateChunk = new char[chunkSize];
                std::memcpy(fStateChunk, chunkStr.buffer(), chunkStr.length());
                fStateChunk[chunkSize] = '\0';

                for (std::size_t i=0; i<chunkSize; ++i)
                {
                    if (fStateChunk[i] == '\xff')
                        fStateChunk[i] = '\0';
                }

                return chunkSize;
            }

            *(void**)ptr = fStateChunk;
            break;

        case effSetChunk:
        {
            if (value <= 1 || ptr == nullptr)
                return 0;

            const char* key   = (const char*)ptr;
            const char* value = nullptr;

            for (;;)
            {
                if (key[0] == '\0')
                    break;

                value = key+(std::strlen(key)+1);

                setStateFromUI(key, value);

                if (fVstUI != nullptr)
                    fVstUI->setStateFromPlugin(key, value);

                // get next key
                key = value+(std::strlen(value)+1);
            }

            return 1;
        }
#endif // DISTRHO_PLUGIN_WANT_STATE

#if DISTRHO_PLUGIN_HAS_MIDI_INPUT
        case effProcessEvents:
            if (const VstEvents* const events = (const VstEvents*)ptr)
            {
                if (events->numEvents == 0)
                    break;

                for (int i=0, count=events->numEvents; i < count; ++i)
                {
                    const VstMidiEvent* const vstMidiEvent((const VstMidiEvent*)events->events[i]);

                    if (vstMidiEvent == nullptr)
                        break;
                    if (vstMidiEvent->type != kVstMidiType)
                        continue;
                    if (fMidiEventCount >= kMaxMidiEvents)
                        break;

                    MidiEvent& midiEvent(fMidiEvents[fMidiEventCount++]);
                    midiEvent.frame  = vstMidiEvent->deltaFrames;
                    midiEvent.size   = 3;
                    std::memcpy(midiEvent.data, vstMidiEvent->midiData, sizeof(uint8_t)*3);
                }
            }
            break;
#endif

        case effCanBeAutomated:
            if (index < static_cast<int32_t>(fPlugin.getParameterCount()))
            {
                const uint32_t hints(fPlugin.getParameterHints(index));

                // must be automable, and not output
                if ((hints & kParameterIsAutomable) != 0 && (hints & kParameterIsOutput) == 0)
                    return 1;
            }
            break;

#if DISTRHO_PLUGIN_HAS_MIDI_INPUT || DISTRHO_PLUGIN_HAS_MIDI_OUTPUT || DISTRHO_PLUGIN_WANT_TIMEPOS || DISTRHO_OS_MAC
        case effCanDo:
            if (const char* const canDo = (const char*)ptr)
            {
# if DISTRHO_OS_MAC
                if (std::strcmp(canDo, "hasCockosViewAsConfig") == 0)
                {
                    fUsingNsView = true;
                    return 0xbeef0000;
                }
# endif
# if DISTRHO_PLUGIN_HAS_MIDI_INPUT
                if (std::strcmp(canDo, "receiveVstEvents") == 0)
                    return 1;
                if (std::strcmp(canDo, "receiveVstMidiEvent") == 0)
                    return 1;
# endif
# if DISTRHO_PLUGIN_HAS_MIDI_OUTPUT
                if (std::strcmp(canDo, "sendVstEvents") == 0)
                    return 1;
                if (std::strcmp(canDo, "sendVstMidiEvent") == 0)
                    return 1;
# endif
# if DISTRHO_PLUGIN_WANT_TIMEPOS
                if (std::strcmp(canDo, "receiveVstTimeInfo") == 0)
                    return 1;
# endif
            }
            break;
#endif

        //case effStartProcess:
        //case effStopProcess:
        // unused
        //    break;
        }

        return 0;
    }

    float vst_getParameter(const int32_t index)
    {
        const ParameterRanges& ranges(fPlugin.getParameterRanges(index));
        return ranges.getNormalizedValue(fPlugin.getParameterValue(index));
    }

    void vst_setParameter(const int32_t index, const float value)
    {
        const ParameterRanges& ranges(fPlugin.getParameterRanges(index));
        const float realValue(ranges.getUnnormalizedValue(value));
        fPlugin.setParameterValue(index, realValue);

#if DISTRHO_PLUGIN_HAS_UI
        if (fVstUI != nullptr)
            setParameterValueFromPlugin(index, realValue);
#endif
    }

    void vst_processReplacing(const float** const inputs, float** const outputs, const int32_t sampleFrames)
    {
#if DISTRHO_PLUGIN_WANT_TIMEPOS
        static const int kWantVstTimeFlags(kVstTransportPlaying|kVstPpqPosValid|kVstTempoValid|kVstTimeSigValid);

        if (const VstTimeInfo* const vstTimeInfo = (const VstTimeInfo*)fAudioMaster(fEffect, audioMasterGetTime, 0, kWantVstTimeFlags, nullptr, 0.0f))
        {
            fTimePosition.frame     =   vstTimeInfo->samplePos;
            fTimePosition.playing   =  (vstTimeInfo->flags & kVstTransportPlaying);
            fTimePosition.bbt.valid = ((vstTimeInfo->flags & kVstTempoValid) != 0 || (vstTimeInfo->flags & kVstTimeSigValid) != 0);

            // ticksPerBeat is not possible with VST
            fTimePosition.bbt.ticksPerBeat = 960.0;

            if (vstTimeInfo->flags & kVstTempoValid)
                fTimePosition.bbt.beatsPerMinute = vstTimeInfo->tempo;
            else
                fTimePosition.bbt.beatsPerMinute = 120.0;

            if (vstTimeInfo->flags & (kVstPpqPosValid|kVstTimeSigValid))
            {
                const int    ppqPerBar = vstTimeInfo->timeSigNumerator * 4 / vstTimeInfo->timeSigDenominator;
                const double barBeats  = (std::fmod(vstTimeInfo->ppqPos, ppqPerBar) / ppqPerBar) * vstTimeInfo->timeSigDenominator;
                const double rest      =  std::fmod(barBeats, 1.0);

                fTimePosition.bbt.bar         = int(vstTimeInfo->ppqPos)/ppqPerBar + 1;
                fTimePosition.bbt.beat        = barBeats-rest+1;
                fTimePosition.bbt.tick        = rest*fTimePosition.bbt.ticksPerBeat+0.5;
                fTimePosition.bbt.beatsPerBar = vstTimeInfo->timeSigNumerator;
                fTimePosition.bbt.beatType    = vstTimeInfo->timeSigDenominator;
            }
            else
            {
                fTimePosition.bbt.bar         = 1;
                fTimePosition.bbt.beat        = 1;
                fTimePosition.bbt.tick        = 0;
                fTimePosition.bbt.beatsPerBar = 4.0f;
                fTimePosition.bbt.beatType    = 4.0f;
            }

            fTimePosition.bbt.barStartTick = fTimePosition.bbt.ticksPerBeat*fTimePosition.bbt.beatsPerBar*(fTimePosition.bbt.bar-1);

            fPlugin.setTimePosition(fTimePosition);
        }
#endif

#if DISTRHO_PLUGIN_HAS_MIDI_INPUT
        fPlugin.run(inputs, outputs, sampleFrames, fMidiEvents, fMidiEventCount);
        fMidiEventCount = 0;
#else
        fPlugin.run(inputs, outputs, sampleFrames);
#endif

#if DISTRHO_PLUGIN_HAS_UI
        if (fVstUI == nullptr)
            return;

        for (uint32_t i=0, count=fPlugin.getParameterCount(); i < count; ++i)
        {
            if (fPlugin.isParameterOutput(i))
                setParameterValueFromPlugin(i, fPlugin.getParameterValue(i));
        }
#endif
    }

    // -------------------------------------------------------------------

    friend class UIVst;

private:
    // VST stuff
    const audioMasterCallback fAudioMaster;
    AEffect* const fEffect;

    // Plugin
    PluginExporter fPlugin;

    // Temporary data
    char fProgramName[64+1];

#if DISTRHO_PLUGIN_HAS_MIDI_INPUT
    uint32_t  fMidiEventCount;
    MidiEvent fMidiEvents[kMaxMidiEvents];
#endif

#if DISTRHO_PLUGIN_WANT_TIMEPOS
    TimePosition fTimePosition;
#endif

    // UI stuff
#if DISTRHO_PLUGIN_HAS_UI
    UIVst* fVstUI;
    ERect  fVstRect;
# if DISTRHO_OS_MAC
    bool fUsingNsView;
# endif
#endif

#if DISTRHO_PLUGIN_WANT_STATE
    char*     fStateChunk;
    StringMap fStateMap;
#endif

    // -------------------------------------------------------------------
    // functions called from the plugin side, RT no block

#if DISTRHO_PLUGIN_HAS_UI
    void setParameterValueFromPlugin(const uint32_t index, const float realValue)
    {
        parameterValues[index] = realValue;
        parameterChecks[index] = true;
    }
#endif

#if DISTRHO_PLUGIN_WANT_STATE
    // -------------------------------------------------------------------
    // functions called from the UI side, may block

    void setStateFromUI(const char* const key, const char* const newValue) override
    {
        fPlugin.setState(key, newValue);

        // check if we want to save this key
        if (! fPlugin.wantStateKey(key))
            return;

        // check if key already exists
        for (StringMap::iterator it=fStateMap.begin(), ite=fStateMap.end(); it != ite; ++it)
        {
            const d_string& d_key(it->first);

            if (d_key == key)
            {
                it->second = newValue;
                return;
            }
        }

        d_stderr("Failed to find plugin state with key \"%s\"", key);
    }
#endif
};

// -----------------------------------------------------------------------

struct VstObject {
    audioMasterCallback audioMaster;
    PluginVst* plugin;
};

#ifdef VESTIGE_HEADER
# define validObject  effect != nullptr && effect->ptr3 != nullptr
# define validPlugin  effect != nullptr && effect->ptr3 != nullptr && ((VstObject*)effect->ptr3)->plugin != nullptr
# define vstObjectPtr (VstObject*)effect->ptr3
#else
# define validObject  effect != nullptr && effect->object != nullptr
# define validPlugin  effect != nullptr && effect->object != nullptr && ((VstObject*)effect->object)->plugin != nullptr
# define vstObjectPtr (VstObject*)effect->object
#endif

#define pluginPtr     (vstObjectPtr)->plugin

static intptr_t vst_dispatcherCallback(AEffect* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
{
    // first internal init
    const bool doInternalInit = (opcode == -1729 && index == 0xdead && value == 0xf00d);

    if (doInternalInit)
    {
        // set valid but dummy values
        d_lastBufferSize = 512;
        d_lastSampleRate = 44100.0;
    }

    // Create dummy plugin to get data from
    static PluginExporter plugin;

    if (doInternalInit)
    {
        // unset
        d_lastBufferSize = 0;
        d_lastSampleRate = 0.0;

        *(PluginExporter**)ptr = &plugin;
        return 0;
    }

    // handle base opcodes
    switch (opcode)
    {
    case effOpen:
        if (VstObject* const obj = vstObjectPtr)
        {
            // this must always be valid
            DISTRHO_SAFE_ASSERT_RETURN(obj->audioMaster != nullptr, 0);

            // some hosts call effOpen twice
            DISTRHO_SAFE_ASSERT_RETURN(obj->plugin == nullptr, 1);

            audioMasterCallback audioMaster = (audioMasterCallback)obj->audioMaster;

            d_lastBufferSize = audioMaster(effect, audioMasterGetBlockSize, 0, 0, nullptr, 0.0f);
            d_lastSampleRate = audioMaster(effect, audioMasterGetSampleRate, 0, 0, nullptr, 0.0f);

            // some hosts are not ready at this point or return 0 buffersize/samplerate
            if (d_lastBufferSize == 0)
                d_lastBufferSize = 2048;
            if (d_lastSampleRate <= 0.0)
                d_lastSampleRate = 44100.0;

            obj->plugin = new PluginVst(audioMaster, effect);
            return 1;
        }
        return 0;

    case effClose:
        if (VstObject* const obj = vstObjectPtr)
        {
            if (obj->plugin != nullptr)
            {
                delete obj->plugin;
                obj->plugin = nullptr;
            }

#if 0
            /* This code invalidates the object created in VSTPluginMain
             * Probably not safe against all hosts */
            obj->audioMaster = nullptr;
# ifdef VESTIGE_HEADER
            effect->ptr3 = nullptr;
# else
            vstObjectPtr = nullptr;
# endif
            delete obj;
#endif

            return 1;
        }
        //delete effect;
        return 0;

    case effGetParamLabel:
        if (ptr != nullptr && index < static_cast<int32_t>(plugin.getParameterCount()))
        {
            DISTRHO::strncpy((char*)ptr, plugin.getParameterUnit(index), 8);
            return 1;
        }
        return 0;

    case effGetParamName:
        if (ptr != nullptr && index < static_cast<int32_t>(plugin.getParameterCount()))
        {
            DISTRHO::strncpy((char*)ptr, plugin.getParameterName(index), 16);
            return 1;
        }
        return 0;

    case effGetPlugCategory:
#if DISTRHO_PLUGIN_IS_SYNTH
        return kPlugCategSynth;
#else
        return kPlugCategEffect;
#endif

    case effGetEffectName:
        if (ptr != nullptr)
        {
            DISTRHO::strncpy((char*)ptr, plugin.getName(), 64);
            return 1;
        }
        return 0;

    case effGetVendorString:
        if (ptr != nullptr)
        {
            DISTRHO::strncpy((char*)ptr, plugin.getMaker(), 64);
            return 1;
        }
        return 0;

    case effGetProductString:
        if (ptr != nullptr)
        {
            DISTRHO::strncpy((char*)ptr, plugin.getLabel(), 64);
            return 1;
        }
        return 0;

    case effGetVendorVersion:
        return plugin.getVersion();

    case effGetVstVersion:
        return kVstVersion;
    };

    // handle advanced opcodes
    if (validPlugin)
        return pluginPtr->vst_dispatcher(opcode, index, value, ptr, opt);

    return 0;
}

static float vst_getParameterCallback(AEffect* effect, int32_t index)
{
    if (validPlugin)
        return pluginPtr->vst_getParameter(index);
    return 0.0f;
}

static void vst_setParameterCallback(AEffect* effect, int32_t index, float value)
{
    if (validPlugin)
        pluginPtr->vst_setParameter(index, value);
}

static void vst_processCallback(AEffect* effect, float** inputs, float** outputs, int32_t sampleFrames)
{
    if (validPlugin)
        pluginPtr->vst_processReplacing(const_cast<const float**>(inputs), outputs, sampleFrames);
}

static void vst_processReplacingCallback(AEffect* effect, float** inputs, float** outputs, int32_t sampleFrames)
{
    if (validPlugin)
        pluginPtr->vst_processReplacing(const_cast<const float**>(inputs), outputs, sampleFrames);
}

#undef pluginPtr
#undef validObject
#undef validPlugin
#undef vstObjectPtr

// -----------------------------------------------------------------------

END_NAMESPACE_DISTRHO

DISTRHO_PLUGIN_EXPORT
#if DISTRHO_OS_WINDOWS || DISTRHO_OS_MAC
const AEffect* VSTPluginMain(audioMasterCallback audioMaster);
#else
const AEffect* VSTPluginMain(audioMasterCallback audioMaster) asm ("main");
#endif

DISTRHO_PLUGIN_EXPORT
const AEffect* VSTPluginMain(audioMasterCallback audioMaster)
{
    USE_NAMESPACE_DISTRHO

    // old version
    if (audioMaster(nullptr, audioMasterVersion, 0, 0, nullptr, 0.0f) == 0)
        return nullptr;

    // first internal init
    PluginExporter* plugin = nullptr;
    vst_dispatcherCallback(nullptr, -1729, 0xdead, 0xf00d, &plugin, 0.0f);
    DISTRHO_SAFE_ASSERT_RETURN(plugin != nullptr, nullptr);

    AEffect* const effect(new AEffect);
    std::memset(effect, 0, sizeof(AEffect));

    // vst fields
    effect->magic    = kEffectMagic;
    effect->uniqueID = plugin->getUniqueId();
#ifdef VESTIGE_HEADER
    int32_t* const version = (int32_t*)&effect->unknown1;
    *version = plugin->getVersion();
#else
    effect->version = plugin->getVersion();
#endif

    // VST doesn't support parameter outputs, hide them
    int numParams = 0;
    bool outputsReached = false;

    for (uint32_t i=0, count=plugin->getParameterCount(); i < count; ++i)
    {
        if (! plugin->isParameterOutput(i))
        {
            // parameter outputs must be all at the end
            DISTRHO_SAFE_ASSERT_BREAK(! outputsReached);
            ++numParams;
            continue;
        }
        outputsReached = true;
    }

    // plugin fields
    effect->numParams   = numParams;
    effect->numPrograms = 1;
    effect->numInputs   = DISTRHO_PLUGIN_NUM_INPUTS;
    effect->numOutputs  = DISTRHO_PLUGIN_NUM_OUTPUTS;

    // plugin flags
    effect->flags |= effFlagsCanReplacing;
#if DISTRHO_PLUGIN_IS_SYNTH
    effect->flags |= effFlagsIsSynth;
#endif
#if DISTRHO_PLUGIN_HAS_UI
    effect->flags |= effFlagsHasEditor;
#endif
#if DISTRHO_PLUGIN_WANT_STATE
    effect->flags |= effFlagsProgramChunks;
#endif

    // static calls
    effect->dispatcher   = vst_dispatcherCallback;
    effect->process      = vst_processCallback;
    effect->getParameter = vst_getParameterCallback;
    effect->setParameter = vst_setParameterCallback;
    effect->processReplacing = vst_processReplacingCallback;

    // pointers
    VstObject* const obj(new VstObject());
    obj->audioMaster = audioMaster;
    obj->plugin      = nullptr;
#ifdef VESTIGE_HEADER
    effect->ptr3   = obj;
#else
    effect->object = obj;
#endif

    return effect;
}

// -----------------------------------------------------------------------
