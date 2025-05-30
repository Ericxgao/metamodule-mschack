#include "mscHack.hpp"

#define nCHANNELS 3
#define CHANNEL_H 90
#define CHANNEL_Y 65
#define CHANNEL_X 10

#define MAX_nWAVES 7
#define MAX_DETUNE 20 // Hz

#define freqMAX 300.0f
#define ADS_MAX_TIME_SECONDS 0.5f

#define WAVE_BUFFER_LEN (192000 / 20) // (9600) based on quality for 20Hz at max sample rate 192000

typedef struct
{
    int state;
    int a, d, r;
    int acount, dcount, rcount, fadecount;
    float fainc, frinc, fadeinc;
    float out;
    bool bTrig;
} ADR_STRUCT;

struct OSC_PARAM_STRUCT
{
    int wavetype;
    int filtertype;

    // wave
    float phase[MAX_nWAVES];
    float freq[MAX_nWAVES];

    // filter
    float q, f;

    float lp1[2] = {}, bp1[2] = {};

    // ads
    ADR_STRUCT adr_wave;
};

//-----------------------------------------------------
// Module Definition
//
//-----------------------------------------------------
struct Osc_3Ch : Module
{
    enum WaveTypes
    {
        WAVE_SIN,
        WAVE_TRI,
        WAVE_SQR,
        WAVE_SAW,
        WAVE_NOISE,
        nWAVEFORMS
    };

    enum ParamIds
    {
        PARAM_DELAY,
        PARAM_ATT = PARAM_DELAY + nCHANNELS,
        PARAM_REL = PARAM_ATT + nCHANNELS,
        PARAM_REZ = PARAM_REL + nCHANNELS,
        PARAM_WAVES = PARAM_REZ + nCHANNELS,
        PARAM_CUTOFF = PARAM_WAVES + (nWAVEFORMS * nCHANNELS),
        PARAM_RES = PARAM_CUTOFF + nCHANNELS,
        PARAM_OUTLVL = PARAM_RES + nCHANNELS,
        PARAM_FILTER_MODE = PARAM_OUTLVL + nCHANNELS,
        PARAM_nWAVES = PARAM_FILTER_MODE + nCHANNELS,
        PARAM_SPREAD = PARAM_nWAVES + nCHANNELS,
        PARAM_DETUNE = PARAM_SPREAD + nCHANNELS,
        nPARAMS = PARAM_DETUNE + nCHANNELS
    };

    enum InputIds
    {
        IN_VOCT,
        IN_TRIG = IN_VOCT + nCHANNELS,
        IN_FILTER = IN_TRIG + nCHANNELS,
        IN_REZ = IN_FILTER + nCHANNELS,
        IN_LEVEL = IN_REZ + nCHANNELS,
        nINPUTS = IN_LEVEL + nCHANNELS,
    };

    enum OutputIds
    {
        OUTPUT_AUDIO,
        nOUTPUTS = OUTPUT_AUDIO + (nCHANNELS * 2)
    };

    enum ADRSTATES
    {
        ADR_OFF,
        ADR_FADE,
        ADR_WAIT_PHASE,
        ADR_FADE_OUT,
        ADR_ATTACK,
        ADR_DELAY,
        ADR_RELEASE
    };

    enum FILTER_TYPES
    {
        FILTER_OFF,
        FILTER_LP,
        FILTER_HP,
        FILTER_BP,
        FILTER_NT
    };

    bool m_bInitialized = false;

    dsp::SchmittTrigger m_SchTrig[nCHANNELS];

    OSC_PARAM_STRUCT m_Wave[nCHANNELS] = {};

    // waveforms
    float m_BufferWave[nWAVEFORMS][WAVE_BUFFER_LEN] = {};

    float m_DetuneIn[nCHANNELS] = {};
    float m_Detune[nCHANNELS][MAX_nWAVES][MAX_nWAVES];

    float m_SpreadIn[nCHANNELS] = {};
    float m_Pan[nCHANNELS][MAX_nWAVES][MAX_nWAVES][2];

    int m_nWaves[nCHANNELS] = {};

    // Contructor
    Osc_3Ch()
    {
        config(nPARAMS, nINPUTS, nOUTPUTS);

        for (int i = 0; i < nCHANNELS; i++)
        {
            configParam(PARAM_DELAY + i, 0.0f, 1.0f, 0.0f, "Delay");
            configParam(PARAM_ATT + i, 0.0f, 1.0f, 0.0f, "Attack");
            configParam(PARAM_REL + i, 0.0f, 1.0f, 0.0f, "Release");

            configParam(PARAM_REZ + i, 0.0f, 1.0f, 0.0f, "Not Used");
            configParam(PARAM_WAVES + i, 0.0f, 1.0f, 0.0f, "Not Used");

            configParam(PARAM_CUTOFF + i, 0.0f, 0.1f, 0.0f, "Filter Cutoff");
            configParam(PARAM_RES + i, 0.0f, 1.0f, 0.0f, "Filter Resonance");

            configParam(PARAM_OUTLVL + i, 0.0f, 1.0f, 0.0f, "Output Level");
            configSwitch(PARAM_FILTER_MODE + i, 0.0f, 4.0f, 0.0f, "Filter Type",
                         {"Off", "Low Pass", "High Pass", "Band Pass", "Notch"});
            configParam(PARAM_nWAVES + i, 0.0f, 6.0f, 0.0f, "Number of Waves");
            configParam(PARAM_SPREAD + i, 0.0f, 1.0f, 0.0f, "Stereo Spread");
            configParam(PARAM_DETUNE + i, 0.0f, 0.05f, 0.0f, "Detune");

            auto s = std::to_string(i + 1);
            configInput(IN_VOCT + i, "V/Oct " + s);
            configInput(IN_TRIG + i, "Trigger " + s);
            configInput(IN_FILTER + i, "Cutoff " + s);
            configInput(IN_REZ + i, "Resonance " + s);
            configInput(IN_LEVEL + i, "Level " + s);

            configOutput(OUTPUT_AUDIO + i * 2, "Left " + s);
            configOutput(OUTPUT_AUDIO + i * 2 + 1, "Right " + s);
        }
    }

    //-----------------------------------------------------
    // MynWaves_Knob
    //-----------------------------------------------------
    struct MynWaves_Knob : Knob_Yellow3_20_Snap
    {
        Osc_3Ch *mymodule;
        int param;

        void onChange(const event::Change &e) override
        {
            auto paramQuantity = getParamQuantity();

            mymodule = (Osc_3Ch *)paramQuantity->module;

            if (mymodule)
            {
                param = paramQuantity->paramId - Osc_3Ch::PARAM_nWAVES;
                mymodule->m_nWaves[param] = (int)(paramQuantity->getValue());
            }

            RoundKnob::onChange(e);
        }
    };

    //-----------------------------------------------------
    // MyEQHi_Knob
    //-----------------------------------------------------
    struct MyDetune_Knob : Knob_Yellow3_20
    {
        Osc_3Ch *mymodule;
        int param;

        void onChange(const event::Change &e) override
        {
            auto paramQuantity = getParamQuantity();

            mymodule = (Osc_3Ch *)paramQuantity->module;

            if (mymodule)
            {
                param = paramQuantity->paramId - Osc_3Ch::PARAM_DETUNE;

                mymodule->m_DetuneIn[param] = paramQuantity->getValue();
                mymodule->CalcDetune(param);
            }

            RoundKnob::onChange(e);
        }
    };

    //-----------------------------------------------------
    // MyEQHi_Knob
    //-----------------------------------------------------
    struct MySpread_Knob : Knob_Yellow3_20
    {
        Osc_3Ch *mymodule;
        int param;

        void onChange(const event::Change &e) override
        {
            auto paramQuantity = getParamQuantity();

            mymodule = (Osc_3Ch *)paramQuantity->module;

            if (mymodule)
            {
                param = paramQuantity->paramId - Osc_3Ch::PARAM_SPREAD;

                mymodule->m_SpreadIn[param] = paramQuantity->getValue();
                mymodule->CalcSpread(param);
            }

            RoundKnob::onChange(e);
        }
    };

    // Overrides
    void process(const ProcessArgs &args) override;
    json_t *dataToJson() override;
    void dataFromJson(json_t *rootJ) override;
    void onRandomize() override;
    void onReset() override;

    void CalcSpread(int ch);
    void CalcDetune(int ch);
    void SetWaveLights(void);
    void BuildWaves(void);
    void ChangeFilterCutoff(int ch, float cutfreq);
    void Filter(int ch, float *InL, float *InR);
    float GetWave(int type, float phase);
    float ProcessADR(int ch);
    void GetAudio(int ch, float *pOutL, float *pOutR, float flevel);
};

//-----------------------------------------------------
// Osc_3Ch_WaveSelect
//-----------------------------------------------------
void Osc_3Ch_WaveSelect(void *pClass, int id, int nbutton, bool bOn)
{
    Osc_3Ch *mymodule;

    if (!pClass)
        return;

    mymodule = (Osc_3Ch *)pClass;
    mymodule->m_Wave[id].wavetype = nbutton;
}

//-----------------------------------------------------
// Procedure:   Widget
//
//-----------------------------------------------------

struct Osc_3Ch_Widget : ModuleWidget
{
    MyLEDButtonStrip *m_pButtonWaveSelect[nCHANNELS]{};

    Osc_3Ch_Widget(Osc_3Ch *module)
    {
        int ch, x, y, x2, y2;
        setModule(module);

        // box.size = Vec( 15*21, 380);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/OSC3Channel.svg")));

        addChild(createWidget<ScrewSilver>(Vec(15, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 0)));
        addChild(createWidget<ScrewSilver>(Vec(15, 365)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 365)));

        y = CHANNEL_Y;
        x = CHANNEL_X;

        for (ch = 0; ch < nCHANNELS; ch++)
        {
            x = CHANNEL_X;

            // inputs
            addInput(createInput<MyPortInSmall>(Vec(x, y), module, Osc_3Ch::IN_VOCT + ch));
            addInput(createInput<MyPortInSmall>(Vec(x, y + 43), module, Osc_3Ch::IN_TRIG + ch));

            x2 = x + 32;
            y2 = y + 52;

            m_pButtonWaveSelect[ch] = new MyLEDButtonStrip(
                x2, y2, 11, 11, 5, 8.0, 5, false, DWRGB(180, 180, 180), DWRGB(255, 255, 0),
                MyLEDButtonStrip::TYPE_EXCLUSIVE, ch, module, Osc_3Ch_WaveSelect);
            addChild(m_pButtonWaveSelect[ch]);

            x2 = x + 24;
            y2 = y + 18;

            // params
            addParam(createParam<Knob_Yellow2_26>(Vec(x2, y2), module, Osc_3Ch::PARAM_ATT + ch));

            x2 += 31;

            addParam(createParam<Knob_Yellow2_26>(Vec(x2, y2), module, Osc_3Ch::PARAM_DELAY + ch));

            x2 += 31;

            addParam(createParam<Knob_Yellow2_26>(Vec(x2, y2), module, Osc_3Ch::PARAM_REL + ch));

            // waves/detune/spread
            x2 = x + 149;
            y2 = y + 56;

            addParam(createParam<Osc_3Ch::MynWaves_Knob>(Vec(x + 129, y + 11), module,
                                                         Osc_3Ch::PARAM_nWAVES + ch));
            addParam(createParam<Osc_3Ch::MyDetune_Knob>(Vec(x + 116, y + 48), module,
                                                         Osc_3Ch::PARAM_DETUNE + ch));
            addParam(createParam<Osc_3Ch::MySpread_Knob>(Vec(x + 116 + 28, y + 48), module,
                                                         Osc_3Ch::PARAM_SPREAD + ch));

            // inputs
            x2 = x + 178;
            y2 = y + 51;

            addInput(createInput<MyPortInSmall>(Vec(x2, y2), module, Osc_3Ch::IN_FILTER + ch));
            x2 += 36;
            addInput(createInput<MyPortInSmall>(Vec(x2, y2), module, Osc_3Ch::IN_REZ + ch));
            x2 += 40;
            addInput(createInput<MyPortInSmall>(Vec(x2, y2), module, Osc_3Ch::IN_LEVEL + ch));

            // filter
            y2 = y + 6;
            x2 = x + 167;
            addParam(createParam<Knob_Green1_40>(Vec(x2, y2), module, Osc_3Ch::PARAM_CUTOFF + ch));
            addParam(createParam<FilterSelectToggle>(Vec(x2 + 43, y2 + 2), module,
                                                     Osc_3Ch::PARAM_FILTER_MODE + ch));
            addParam(
                createParam<Knob_Purp1_20>(Vec(x2 + 46, y2 + 20), module, Osc_3Ch::PARAM_RES + ch));

            // main level
            addParam(
                createParam<Knob_Blue2_40>(Vec(x2 + 76, y2), module, Osc_3Ch::PARAM_OUTLVL + ch));

            // outputs
            addOutput(createOutput<MyPortOutSmall>(Vec(x + 283, y + 4), module,
                                                   Osc_3Ch::OUTPUT_AUDIO + (ch * 2)));
            addOutput(createOutput<MyPortOutSmall>(Vec(x + 283, y + 53), module,
                                                   Osc_3Ch::OUTPUT_AUDIO + (ch * 2) + 1));

            y += CHANNEL_H;
        }

        if (module)
        {
            module->m_bInitialized = true;

            module->BuildWaves();
            module->SetWaveLights();
        }
    }

    void step() override
    {
        auto omod = dynamic_cast<Osc_3Ch *>(module);
        if (omod)
        {
            for (auto ch = 0; ch < nCHANNELS; ch++)
                m_pButtonWaveSelect[ch]->Set(omod->m_Wave[ch].wavetype, true);
        }
        Widget::step();
    }
};

//-----------------------------------------------------
// Procedure:   dataToJson
//
//-----------------------------------------------------
json_t *Osc_3Ch::dataToJson()
{
    json_t *gatesJ;
    json_t *rootJ = json_object();

    // wavetypes
    gatesJ = json_array();

    for (int i = 0; i < nCHANNELS; i++)
    {
        json_t *gateJ = json_integer(m_Wave[i].wavetype);
        json_array_append_new(gatesJ, gateJ);
    }

    json_object_set_new(rootJ, "wavetypes", gatesJ);

    return rootJ;
}

//-----------------------------------------------------
// Procedure:   dataFromJson
//
//-----------------------------------------------------
void Osc_3Ch::dataFromJson(json_t *rootJ)
{
    int i;
    json_t *StepsJ;

    // wave select
    StepsJ = json_object_get(rootJ, "wavetypes");

    if (StepsJ)
    {
        for (i = 0; i < nCHANNELS; i++)
        {
            json_t *gateJ = json_array_get(StepsJ, i);

            if (gateJ)
                m_Wave[i].wavetype = json_integer_value(gateJ);
        }
    }

    // set up parameters
    for (i = 0; i < nCHANNELS; i++)
    {
        m_nWaves[i] = (int)(params[PARAM_nWAVES + i].value);

        m_SpreadIn[i] = params[PARAM_SPREAD + i].value;
        CalcSpread(i);
        m_DetuneIn[i] = params[PARAM_DETUNE + i].value;
        CalcDetune(i);
    }

    SetWaveLights();
}

//-----------------------------------------------------
// Procedure:   reset
//
//-----------------------------------------------------
void Osc_3Ch::onReset() {}

//-----------------------------------------------------
// Procedure:   randomize
//
//-----------------------------------------------------
void Osc_3Ch::onRandomize()
{
    int ch;

    for (ch = 0; ch < nCHANNELS; ch++)
    {
        m_Wave[ch].wavetype = (int)(random::uniform() * (nWAVEFORMS - 1));
    }

    SetWaveLights();
}

//-----------------------------------------------------
// Procedure:   SetWaveLights
//
//-----------------------------------------------------
void Osc_3Ch::SetWaveLights(void) {}

//-----------------------------------------------------
// Procedure:   initialize
//
//-----------------------------------------------------
//#define DEG2RAD( x ) ( ( x ) * ( 3.14159f / 180.0f ) )
void Osc_3Ch::BuildWaves(void)
{
    int i;
    float finc, pos, val;

    finc = 360.0 / WAVE_BUFFER_LEN;
    pos = 0;

    // create sin wave
    for (i = 0; i < WAVE_BUFFER_LEN; i++)
    {
        m_BufferWave[WAVE_SIN][i] = sin(DEG2RAD(pos));
        pos += finc;
    }

    // create sqr wave
    for (i = 0; i < WAVE_BUFFER_LEN; i++)
    {
        if (i < WAVE_BUFFER_LEN / 2)
            m_BufferWave[WAVE_SQR][i] = 1.0;
        else
            m_BufferWave[WAVE_SQR][i] = -1.0;
    }

    finc = 2.0 / (float)WAVE_BUFFER_LEN;
    val = 1.0;

    // create saw wave
    for (i = 0; i < WAVE_BUFFER_LEN; i++)
    {
        m_BufferWave[WAVE_SAW][i] = val;

        val -= finc;
    }

    finc = 4 / (float)WAVE_BUFFER_LEN;
    val = 0;

    // create tri wave
    for (i = 0; i < WAVE_BUFFER_LEN; i++)
    {
        m_BufferWave[WAVE_TRI][i] = val;

        if (i < WAVE_BUFFER_LEN / 4)
            val += finc;
        else if (i < (WAVE_BUFFER_LEN / 4) * 3)
            val -= finc;
        else if (i < WAVE_BUFFER_LEN)
            val += finc;
    }
}

//-----------------------------------------------------
// Procedure:   GetAudio
//
//-----------------------------------------------------
float Osc_3Ch::GetWave(int type, float phase)
{
    float fval = 0.0;
    float ratio = (float)(WAVE_BUFFER_LEN - 1) / APP->engine->getSampleRate();

    switch (type)
    {
    case WAVE_SIN:
    case WAVE_TRI:
    case WAVE_SQR:
    case WAVE_SAW:
        fval = m_BufferWave[type][int((phase * ratio) + 0.5)];
        break;

    case WAVE_NOISE:
        fval = (random::uniform() > 0.5) ? (random::uniform() * -1.0) : random::uniform();
        break;

    default:
        break;
    }

    return fval;
}

//-----------------------------------------------------
// Procedure:   ProcessADS
//
//-----------------------------------------------------
float Osc_3Ch::ProcessADR(int ch)
{
    ADR_STRUCT *padr;

    padr = &m_Wave[ch].adr_wave;

    // retrig the adsr
    if (padr->bTrig)
    {
        padr->state = ADR_FADE;

        padr->fadecount = 900;
        padr->fadeinc = padr->out / (float)padr->fadecount;

        padr->acount =
            40 + (int)(params[PARAM_ATT + ch].value * 2.0f * APP->engine->getSampleRate());
        padr->fainc = 1.0f / (float)padr->acount;

        padr->dcount = (int)(params[PARAM_DELAY + ch].value * 4.0f * APP->engine->getSampleRate());

        padr->rcount =
            20 + (int)(params[PARAM_REL + ch].value * 10.0f * APP->engine->getSampleRate());
        padr->frinc = 1.0f / (float)padr->rcount;

        padr->bTrig = false;
    }

    // process
    switch (padr->state)
    {
    case ADR_FADE:
        if (--padr->fadecount <= 0)
        {
            padr->state = ADR_ATTACK;
            padr->out = 0.0f;
            m_Wave[ch].phase[0] = 0.0f;
            m_Wave[ch].phase[1] = 0.0f;
            m_Wave[ch].phase[2] = 0.0f;
            m_Wave[ch].phase[3] = 0.0f;
            m_Wave[ch].phase[4] = 0.0f;
            m_Wave[ch].phase[5] = 0.0f;
            m_Wave[ch].phase[6] = 0.0f;
        }
        else
        {
            padr->out -= padr->fadeinc;
        }

        break;

    case ADR_OFF:
        padr->out = 0.0f;
        break;

    case ADR_ATTACK:

        if (--padr->acount <= 0)
        {
            padr->state = ADR_DELAY;
        }
        else
        {
            padr->out += padr->fainc;
        }

        break;

    case ADR_DELAY:
        padr->out = 1.0f;
        if (--padr->dcount <= 0)
        {
            padr->state = ADR_RELEASE;
        }
        break;

    case ADR_RELEASE:

        if (--padr->rcount <= 0)
        {
            padr->state = ADR_OFF;
            padr->out = 0.0f;
        }
        else
        {
            padr->out -= padr->frinc;
        }

        break;
    }

    return clamp(padr->out, 0.0f, 1.0f);
}

//-----------------------------------------------------
// Procedure:   ChangeFilterCutoff
//
//-----------------------------------------------------
void Osc_3Ch::ChangeFilterCutoff(int ch, float cutfreq)
{
    float fx, fx2, fx3, fx5, fx7;

    // clamp at 1.0 and 20/samplerate
    cutfreq = fmax(cutfreq, 20 / APP->engine->getSampleRate());
    cutfreq = fmin(cutfreq, 1.0);

    // calculate eq rez freq
    fx = 3.141592 * (cutfreq * 0.026315789473684210526315789473684) * 2 * 3.141592;
    fx2 = fx * fx;
    fx3 = fx2 * fx;
    fx5 = fx3 * fx2;
    fx7 = fx5 * fx2;

    m_Wave[ch].f = 2.0 * (fx - (fx3 * 0.16666666666666666666666666666667) +
                          (fx5 * 0.0083333333333333333333333333333333) -
                          (fx7 * 0.0001984126984126984126984126984127));
}

//-----------------------------------------------------
// Procedure:   Filter
//
//-----------------------------------------------------
#define MULTI (0.33333333333333333333333333333333f)
void Osc_3Ch::Filter(int ch, float *InL, float *InR)
{
    OSC_PARAM_STRUCT *p;
    float rez, hp1;
    float input[2], out[2], lowpass, bandpass, highpass;

    if ((int)params[PARAM_FILTER_MODE + ch].getValue() == 0)
        return;

    p = &m_Wave[ch];

    rez = 1.0 - params[PARAM_RES + ch].value;

    input[0] = *InL;
    input[1] = *InR;

    // do left and right channels
    for (int i = 0; i < 2; i++)
    {
        input[i] = input[i] + 0.000000001;

        p->lp1[i] = p->lp1[i] + p->f * p->bp1[i];
        hp1 = input[i] - p->lp1[i] - rez * p->bp1[i];
        p->bp1[i] = p->f * hp1 + p->bp1[i];
        lowpass = p->lp1[i];
        highpass = hp1;
        bandpass = p->bp1[i];

        p->lp1[i] = p->lp1[i] + p->f * p->bp1[i];
        hp1 = input[i] - p->lp1[i] - rez * p->bp1[i];
        p->bp1[i] = p->f * hp1 + p->bp1[i];
        lowpass = lowpass + p->lp1[i];
        highpass = highpass + hp1;
        bandpass = bandpass + p->bp1[i];

        input[i] = input[i] - 0.000000001;

        p->lp1[i] = p->lp1[i] + p->f * p->bp1[i];
        hp1 = input[i] - p->lp1[i] - rez * p->bp1[i];
        p->bp1[i] = p->f * hp1 + p->bp1[i];

        lowpass = (lowpass + p->lp1[i]) * MULTI;
        highpass = (highpass + hp1) * MULTI;
        bandpass = (bandpass + p->bp1[i]) * MULTI;

        switch ((int)params[PARAM_FILTER_MODE + ch].getValue())
        {
        case FILTER_LP:
            out[i] = lowpass;
            break;
        case FILTER_HP:
            out[i] = highpass;
            break;
        case FILTER_BP:
            out[i] = bandpass;
            break;
        case FILTER_NT:
            out[i] = lowpass + highpass;
            break;
        default:
            break;
        }
    }

    *InL = out[0];
    *InR = out[1];
}

//-----------------------------------------------------
// Procedure:   CalcSpread
//
//-----------------------------------------------------
typedef struct
{
    float pan[2];
    float maxdetune;
} PAN_DETUNE;

PAN_DETUNE pandet[7][7] = {
    {{{1.0, 1.0}, 0.0},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0}},
    {{{1.0, 0.5}, 0.1},
     {{0.5, 1.0}, 0.2},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0}},
    {{{1.0, 0.5}, 0.3},
     {{1.0, 1.0}, 0.0},
     {{0.5, 1.0}, 0.2},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0}},
    {{{1.0, 0.3}, 0.4},
     {{1.0, 0.5}, 0.2},
     {{0.5, 1.0}, 0.2},
     {{0.3, 1.0}, 0.3},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0}},
    {{{1.0, 0.3}, 0.5},
     {{1.0, 0.5}, 0.4},
     {{1.0, 1.0}, 0.0},
     {{0.5, 1.0}, 0.3},
     {{0.3, 1.0}, 0.1},
     {{0.0, 0.0}, 0.0},
     {{0.0, 0.0}, 0.0}},
    {{{1.0, 0.2}, 0.6},
     {{1.0, 0.3}, 0.4},
     {{1.0, 0.5}, 0.2},
     {{0.5, 1.0}, 0.3},
     {{0.3, 1.0}, 0.5},
     {{0.2, 1.0}, 0.8},
     {{0.0, 0.0}, 0.0}},
    {{{1.0, 0.0}, 0.9},
     {{1.0, 0.2}, 0.7},
     {{1.0, 0.3}, 0.5},
     {{1.0, 1.0}, 0.0},
     {{0.3, 1.0}, 0.4},
     {{0.2, 1.0}, 0.8},
     {{0.0, 1.0}, 1.0}},
};

void Osc_3Ch::CalcSpread(int ch)
{
    int used; // number of waves being used by channel
    int wave; // values for each individual wave

    // calculate pans for each possible number of waves being used
    for (used = 0; used < MAX_nWAVES; used++)
    {
        for (wave = 0; wave <= used; wave++)
        {
            m_Pan[ch][used][wave][0] =
                (1.0 - m_SpreadIn[ch]) + (pandet[used][wave].pan[0] * m_SpreadIn[ch]);
            m_Pan[ch][used][wave][1] =
                (1.0 - m_SpreadIn[ch]) + (pandet[used][wave].pan[1] * m_SpreadIn[ch]);
        }
    }
}

void Osc_3Ch::CalcDetune(int ch)
{
    int used; // number of waves being used by channel
    int wave; // values for each individual wave

    // calculate detunes for each possible number of waves being used
    for (used = 0; used < MAX_nWAVES; used++)
    {
        for (wave = 0; wave <= used; wave++)
            m_Detune[ch][used][wave] = pandet[used][wave].maxdetune * MAX_DETUNE * m_DetuneIn[ch];
    }
}

//-----------------------------------------------------
// Procedure:   GetAudio
//
//-----------------------------------------------------
void Osc_3Ch::GetAudio(int ch, float *pOutL, float *pOutR, float flevel)
{
    float foutL = 0, foutR = 0, cutoff, adr;
    int i;

    for (i = 0; i <= m_nWaves[ch]; i++)
    {
        foutL = GetWave(m_Wave[ch].wavetype, m_Wave[ch].phase[i]) / 2.0;
        foutR = foutL;

        foutL *= m_Pan[ch][m_nWaves[ch]][i][0];
        foutR *= m_Pan[ch][m_nWaves[ch]][i][1];

        // ( 32.7032 is C1 ) ( 4186.01 is C8)
        m_Wave[ch].phase[i] +=
            32.7032f * clamp(powf(2.0f, clamp(inputs[IN_VOCT + ch].getVoltage(), 0.0f, VOCT_MAX)) +
                                 m_Detune[ch][m_nWaves[ch]][i],
                             0.0f, 4186.01f);

        if (m_Wave[ch].phase[i] >= APP->engine->getSampleRate())
            m_Wave[ch].phase[i] = m_Wave[ch].phase[i] - APP->engine->getSampleRate();

        *pOutL += foutL;
        *pOutR += foutR;
    }

    adr = ProcessADR(ch);

    *pOutL = *pOutL * adr * flevel;
    *pOutR = *pOutR * adr * flevel;

    cutoff = clamp(params[PARAM_CUTOFF + ch].getValue() *
                       (inputs[IN_FILTER + ch].getNormalVoltage(CV_MAX10) / CV_MAX10),
                   0.0f, 1.0f);

    ChangeFilterCutoff(ch, cutoff);

    Filter(ch, pOutL, pOutR);
}

//-----------------------------------------------------
// Procedure:   step
//
//-----------------------------------------------------
void Osc_3Ch::process(const ProcessArgs &args)
{
    int ch;
    float outL, outR, flevel;

    if (!m_bInitialized)
        return;

    // check for triggers
    for (ch = 0; ch < nCHANNELS; ch++)
    {
        outL = 0.0;
        outR = 0.0;

        if (inputs[IN_TRIG + ch].isConnected())
        {
            if (m_SchTrig[ch].process(inputs[IN_TRIG + ch].getVoltage()))
            {
                m_Wave[ch].adr_wave.bTrig = true;
            }
        }

        flevel = clamp(params[PARAM_OUTLVL + ch].getValue() +
                           (inputs[IN_LEVEL + ch].getNormalVoltage(0.0) / 5.0f),
                       0.0f, 1.0f);

        GetAudio(ch, &outL, &outR, flevel);

        // outL = clamp( ( outL * AUDIO_MAX ), -AUDIO_MAX, AUDIO_MAX );
        // outR = clamp( ( outR * AUDIO_MAX ), -AUDIO_MAX, AUDIO_MAX );

        outL = outL * AUDIO_MAX;
        outR = outR * AUDIO_MAX;

        outputs[OUTPUT_AUDIO + (ch * 2)].setVoltage(outL);
        outputs[OUTPUT_AUDIO + (ch * 2) + 1].setVoltage(outR);
    }
}

Model *modelOsc_3Ch = createModel<Osc_3Ch, Osc_3Ch_Widget>("Osc_3Ch_Widget");