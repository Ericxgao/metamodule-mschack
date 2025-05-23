#include "mscHack.hpp"

typedef struct
{
    int state;
    float finc;
    int count;
    float fade;
} COMP_STATE;

//-----------------------------------------------------
// Module Definition
//
//-----------------------------------------------------
struct Compressor : Module
{
    enum ParamIds
    {
        PARAM_INGAIN,
        PARAM_OUTGAIN,
        PARAM_THRESHOLD,
        PARAM_RATIO,
        PARAM_ATTACK,
        PARAM_RELEASE,
        PARAM_BYPASS,
        PARAM_SIDE_CHAIN,
        nPARAMS
    };

    enum InputIds
    {
        IN_AUDIOL,
        IN_AUDIOR,
        IN_SIDE_CHAIN,
        nINPUTS
    };

    enum OutputIds
    {
        OUT_AUDIOL,
        OUT_AUDIOR,
        nOUTPUTS
    };

    enum CompState
    {
        COMP_DONE,
        COMP_START,
        COMP_ATTACK,
        COMP_RELEASE,
        COMP_IDLE
    };

    bool m_bInitialized = false;

    bool m_bBypass = false;

    COMP_STATE m_CompL = {};
    COMP_STATE m_CompR = {};
    float m_fThreshold;

    // Contructor
    Compressor()
    {
        config(nPARAMS, nINPUTS, nOUTPUTS, 0);

        configParam(PARAM_INGAIN, 0.0, 4.0, 1.0, "In Gain");
        configParam(PARAM_OUTGAIN, 0.0, 8.0, 1.0, "Out Gain");
        configParam(PARAM_THRESHOLD, 0.0, 0.99, 0.0, "Threshold");
        configParam(PARAM_RATIO, 0.0, 2.0, 0.0, "Ratio");
        configParam(PARAM_ATTACK, 0.0f, 1.0f, 0.0f, "Attack");
        configParam(PARAM_RELEASE, 0.0f, 1.0f, 0.0f, "Release");
        configParam(PARAM_BYPASS, 0.0f, 1.0f, 0.0f, "Bypass");
        configParam(PARAM_SIDE_CHAIN, 0.0f, 1.0f, 0.0f, "Sidechain");

        configInput(IN_AUDIOL, "Left");
        configInput(IN_AUDIOR, "Right");
        configInput(IN_SIDE_CHAIN, "Side CHain");

        configOutput(OUT_AUDIOL, "Left");
        configOutput(OUT_AUDIOR, "Right");

        configBypass(IN_AUDIOL, OUT_AUDIOL);
        configBypass(IN_AUDIOR, OUT_AUDIOR);
    }

    typedef rack::dsp::RingBuffer<float, 8192 * 4> rbuf_t;
    rbuf_t m_rbLEDMeterIn[2], m_rbLEDMeterComp[2], m_rbLEDMeterThreshold, m_rbLEDMeterOut[2];

    // Overrides
    void process(const ProcessArgs &args) override;
    json_t *dataToJson() override;
    void dataFromJson(json_t *rootJ) override;

    bool ProcessCompState(COMP_STATE *pComp, bool bAboveThreshold);
    float Compress(float *pDetectInL, float *pDetectInR);
};

//-----------------------------------------------------
// Compressor_Bypass
//-----------------------------------------------------
void Compressor_Bypass(void *pClass, int id, bool bOn)
{
    Compressor *mymodule;
    mymodule = (Compressor *)pClass;
    mymodule->m_bBypass = bOn;
}

//-----------------------------------------------------
// Procedure:   Widget
//
//-----------------------------------------------------

struct Compressor_Widget : ModuleWidget
{
    // LEDMeterWidget *m_pLEDMeterIn[2]{0, 0};
    // CompressorLEDMeterWidget *m_pLEDMeterThreshold{nullptr};
    // CompressorLEDMeterWidget *m_pLEDMeterComp[2]{0, 0};
    // LEDMeterWidget *m_pLEDMeterOut[2]{0, 0};
    MyLEDButton *m_pButtonBypass{nullptr};

    Compressor_Widget(Compressor *module)
    {
        int x, y, y2;

        // box.size = Vec( 15*8, 380);

        setModule(module);

        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Compressor.svg")));

        x = 10;
        y = 34;

        addChild(createWidget<ScrewSilver>(Vec(15, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 0)));
        addChild(createWidget<ScrewSilver>(Vec(15, 365)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 365)));

        // bypass switch
        m_pButtonBypass = new MyLEDButton(x, y, 11, 11, 8.0, DWRGB(180, 180, 180), DWRGB(255, 0, 0),
                                          MyLEDButton::TYPE_SWITCH, 0, module, Compressor_Bypass);
        addChild(m_pButtonBypass);

        // audio inputs
        addInput(createInput<MyPortInSmall>(Vec(x, y + 32), module, Compressor::IN_AUDIOL));
        addInput(createInput<MyPortInSmall>(Vec(x, y + 78), module, Compressor::IN_AUDIOR));
        addInput(
            createInput<MyPortInSmall>(Vec(x - 1, y + 210), module, Compressor::IN_SIDE_CHAIN));

        // // LED meters - too laggy for metamodule
        // m_pLEDMeterIn[0] = new LEDMeterWidget(x + 22, y + 25, 5, 3, 2, true);
        // addChild(m_pLEDMeterIn[0]);
        // m_pLEDMeterIn[1] = new LEDMeterWidget(x + 28, y + 25, 5, 3, 2, true);
        // addChild(m_pLEDMeterIn[1]);

        // m_pLEDMeterThreshold = new CompressorLEDMeterWidget(true, x + 39, y + 25, 5, 3,
        //                                                     DWRGB(245, 10, 174), DWRGB(96, 4, 68));
        // addChild(m_pLEDMeterThreshold);

        // m_pLEDMeterComp[0] = new CompressorLEDMeterWidget(true, x + 48, y + 25, 5, 3,
        //                                                   DWRGB(0, 128, 255), DWRGB(0, 64, 128));
        // addChild(m_pLEDMeterComp[0]);
        // m_pLEDMeterComp[1] = new CompressorLEDMeterWidget(true, x + 55, y + 25, 5, 3,
        //                                                   DWRGB(0, 128, 255), DWRGB(0, 64, 128));
        // addChild(m_pLEDMeterComp[1]);

        // m_pLEDMeterOut[0] = new LEDMeterWidget(x + 65, y + 25, 5, 3, 2, true);
        // addChild(m_pLEDMeterOut[0]);
        // m_pLEDMeterOut[1] = new LEDMeterWidget(x + 72, y + 25, 5, 3, 2, true);
        // addChild(m_pLEDMeterOut[1]);

        // audio  outputs
        addOutput(
            createOutput<MyPortOutSmall>(Vec(x + 83, y + 32), module, Compressor::OUT_AUDIOL));
        addOutput(
            createOutput<MyPortOutSmall>(Vec(x + 83, y + 78), module, Compressor::OUT_AUDIOR));

        // add param knobs
        y2 = y + 149;
        addParam(
            createParam<Knob_Yellow1_26>(Vec(x + 11, y + 113), module, Compressor::PARAM_INGAIN));
        addParam(
            createParam<Knob_Yellow1_26>(Vec(x + 62, y + 113), module, Compressor::PARAM_OUTGAIN));
        addParam(
            createParam<Knob_Blue2_26>(Vec(x - 5, y2 + 20), module, Compressor::PARAM_SIDE_CHAIN));
        addParam(
            createParam<Knob_Yellow1_26>(Vec(x + 39, y2), module, Compressor::PARAM_THRESHOLD));
        y2 += 40;
        addParam(createParam<Knob_Yellow1_26>(Vec(x + 39, y2), module, Compressor::PARAM_RATIO));
        y2 += 40;
        addParam(createParam<Knob_Yellow1_26>(Vec(x + 39, y2), module, Compressor::PARAM_ATTACK));
        y2 += 40;
        addParam(createParam<Knob_Yellow1_26>(Vec(x + 39, y2), module, Compressor::PARAM_RELEASE));

        if (module)
            module->m_bInitialized = true;
    }

    void step() override
    {
        auto az = dynamic_cast<Compressor *>(module);
        if (az)
        {
            auto apply = [](auto &rb, auto *wid) {
                while (!rb.empty())
                {
                    auto f = rb.shift();
                    wid->Process(f);
                }
            };

            // apply(az->m_rbLEDMeterIn[0], m_pLEDMeterIn[0]);
            // apply(az->m_rbLEDMeterIn[1], m_pLEDMeterIn[1]);
            // apply(az->m_rbLEDMeterComp[0], m_pLEDMeterComp[0]);
            // apply(az->m_rbLEDMeterComp[1], m_pLEDMeterComp[1]);
            // apply(az->m_rbLEDMeterThreshold, m_pLEDMeterThreshold);
            // apply(az->m_rbLEDMeterOut[0], m_pLEDMeterOut[0]);
            // apply(az->m_rbLEDMeterOut[1], m_pLEDMeterOut[1]);

            if (m_pButtonBypass->m_bOn != az->m_bBypass)
                m_pButtonBypass->Set(az->m_bBypass);
        }
        Widget::step();
    }
};

//-----------------------------------------------------
// Procedure:
//
//-----------------------------------------------------
json_t *Compressor::dataToJson()
{
    json_t *rootJ = json_object();

    // reverse state
    json_object_set_new(rootJ, "m_bBypass", json_boolean(m_bBypass));

    return rootJ;
}

//-----------------------------------------------------
// Procedure:   fromJson
//
//-----------------------------------------------------
void Compressor::dataFromJson(json_t *rootJ)
{
    // reverse state
    json_t *revJ = json_object_get(rootJ, "m_bBypass");

    if (revJ)
        m_bBypass = json_is_true(revJ);
}

//-----------------------------------------------------
// Procedure:   ProcessCompStatus
//
//-----------------------------------------------------
#define MAX_ATT_TIME (0.5f) // 500ms
#define MAX_REL_TIME (2.0f) // 2s
bool Compressor::ProcessCompState(COMP_STATE *pComp, bool bAboveThreshold)
{
    bool bCompressing = true;

    // restart compressor if it has finished
    if (bAboveThreshold && (pComp->state == COMP_IDLE))
    {
        pComp->state = COMP_START;
    }
    // ready compressor for restart
    else if (!bAboveThreshold && (pComp->state == COMP_DONE))
    {
        pComp->state = COMP_IDLE;
    }
    else if (!bAboveThreshold && (pComp->state == COMP_ATTACK))
    {
        pComp->count =
            10 + (int)(MAX_REL_TIME * APP->engine->getSampleRate() * params[PARAM_RELEASE].value);
        pComp->fade = 1.0f;
        pComp->finc = 1.0f / (float)pComp->count;
        pComp->state = COMP_RELEASE;
    }

    switch (pComp->state)
    {
    case COMP_START:
        pComp->count =
            10 + (int)(MAX_ATT_TIME * APP->engine->getSampleRate() * params[PARAM_ATTACK].value);

        pComp->state = COMP_ATTACK;
        pComp->finc = (1.0f - pComp->fade) / (float)pComp->count;

        break;

    case COMP_ATTACK:
        if (--pComp->count > 0)
        {
            pComp->fade += pComp->finc;

            if (pComp->fade > 1.0f)
                pComp->fade = 1.0f;
        }
        /*else
        {
            pComp->count = 10 + (int)( MAX_REL_TIME * engineGetSampleRate() * params[ PARAM_RELEASE
        ].value ); pComp->fade = 1.0f; pComp->finc = 1.0f / (float)pComp->count; pComp->state =
        COMP_RELEASE;
        }*/

        break;

    case COMP_RELEASE:
        if (--pComp->count > 0)
        {
            pComp->fade -= pComp->finc;

            if (pComp->fade < 0.0f)
                pComp->fade = 0.0f;
        }
        else
        {
            pComp->fade = 0.0f;
            pComp->state = COMP_DONE;
            bCompressing = false;
        }
        break;

    case COMP_DONE:
        pComp->fade = 0.0f;
        bCompressing = false;
        break;

    case COMP_IDLE:
        pComp->fade = 0.0f;
        bCompressing = false;
        break;
    }

    return bCompressing;
}

//-----------------------------------------------------
// Procedure:   Compress
//
//-----------------------------------------------------
float Compressor::Compress(float *pDetectInL, float *pDetectInR)
{
    float rat, th, finL, finR, compL = 1.0f, compR = 1.0f;

    m_fThreshold = params[PARAM_THRESHOLD].value;
    th = 1.0f - m_fThreshold;
    rat = params[PARAM_RATIO].value;

    finL = fabs(*pDetectInL);

    if (ProcessCompState(&m_CompL, (finL > th)))
        compL = 1.0f - (rat * m_CompL.fade);

    if (pDetectInR)
    {
        finR = fabs(*pDetectInR);

        if (ProcessCompState(&m_CompR, (finR > th)))
            compR = 1.0f - (rat * m_CompR.fade);
    }
    else
    {
        m_CompR.state = COMP_IDLE;
        m_CompR.fade = 0.0;
    }

    return fmin(compL, compR);
}

//-----------------------------------------------------
// Procedure:   step
//
//-----------------------------------------------------
void Compressor::process(const ProcessArgs &args)
{
    float outL, outR, diffL, diffR, fcomp, fside;

    if (!m_bInitialized)
        return;

    outL = inputs[IN_AUDIOL].getVoltageSum() / AUDIO_MAX;
    outR = inputs[IN_AUDIOR].getVoltageSum() / AUDIO_MAX;

    if (!m_bBypass)
    {
        outL = clamp(outL * params[PARAM_INGAIN].getValue(), -1.0f, 1.0f);
        outR = clamp(outR * params[PARAM_INGAIN].getValue(), -1.0f, 1.0f);
    }

    m_rbLEDMeterIn[0].push(outL);
    m_rbLEDMeterIn[1].push(outR);

    diffL = fabs(outL);
    diffR = fabs(outR);

    if (!m_bBypass)
    {
        // compress
        if (inputs[IN_SIDE_CHAIN].isConnected())
        {
            fside = clamp((inputs[IN_SIDE_CHAIN].getNormalVoltage(0.0) / AUDIO_MAX) *
                              params[PARAM_SIDE_CHAIN].getValue(),
                          -1.0f, 1.0f);
            fcomp = Compress(&fside, NULL);
        }
        else
        {
            fcomp = Compress(&outL, &outR);
        }

        outL *= fcomp;
        outR *= fcomp;

        diffL -= fabs(outL);
        diffR -= fabs(outR);

        m_rbLEDMeterComp[0].push(diffL);
        m_rbLEDMeterComp[1].push(diffR);
        m_rbLEDMeterThreshold.push(m_fThreshold);

        outL = clamp(outL * params[PARAM_OUTGAIN].getValue(), -1.0f, 1.0f);
        outR = clamp(outR * params[PARAM_OUTGAIN].getValue(), -1.0f, 1.0f);
    }
    else
    {
        m_rbLEDMeterComp[0].push(0.f);
        m_rbLEDMeterComp[1].push(0.f);
        m_rbLEDMeterThreshold.push(0.f);
    }

    m_rbLEDMeterOut[0].push(outL);
    m_rbLEDMeterOut[1].push(outR);

    outputs[OUT_AUDIOL].setVoltage(outL * AUDIO_MAX);
    outputs[OUT_AUDIOR].setVoltage(outR * AUDIO_MAX);
}

Model *modelCompressor = createModel<Compressor, Compressor_Widget>("Compressor1");