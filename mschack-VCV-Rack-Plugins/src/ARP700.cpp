#include "mscHack.hpp"
#include <iostream>

#define MAX_ARP_PATTERNS 16
#define MAX_ARP_NOTES 7
#define SUBSTEP_PER_NOTE 3

#define SEMI (1.0f / 12.0f)
#define TRIG_OFF_TICKS 10

#define ARP_OFF 0
#define ARP_ON 1
#define ARP_REST 2

typedef struct
{
    int notesused;
    int notes[MAX_ARP_NOTES];
    int onoffsel[MAX_ARP_NOTES][SUBSTEP_PER_NOTE];
    int lensel[MAX_ARP_NOTES][SUBSTEP_PER_NOTE];
    int lenmod[MAX_ARP_NOTES][SUBSTEP_PER_NOTE];
    int legato[MAX_ARP_NOTES];
    int glide[MAX_ARP_NOTES];
    int mode;
    int oct;

} ARP_PATTERN_STRUCT;

typedef struct
{
    bool bPending;
    int pat;
} ARP_PHRASE_CHANGE_STRUCT;

//------------------------------------------------------
// sliding window average
/*#define AVG_ARRAY_LEN   4
#define AVG_AND         (AVG_ARRAY_LEN - 1)

typedef struct
{
    int count;
    int avg[ AVG_ARRAY_LEN ];
    int  tot;
}SLIDING_AVG_STRUCT;*/

typedef struct
{
    // track in clk bpm
    // SLIDING_AVG_STRUCT Avg;
    int tickcount;
    float ftickspersec;
    float fbpm;

    // track sync tick
    float fsynclen;
    float fsynccount;

    bool bClockReset;
    int IgnoreClockCount;

} MAIN_SYNC_CLOCK;

struct PAT_STEP_STRUCT
{
    // timing
    bool bTrig;
    int pat;
    int used;

    int step;
    int virtstep;

    ARP_PHRASE_CHANGE_STRUCT pending;

    // track current arp trig
    int nextcount;
    bool bNextTrig;

    // glide
    float fglideInc;
    int glideCount;
    float fglide;
    float fLastNotePlayed;
    bool bWasLastNotePlayed;

    // voct out
    float fCvStartOut = 0;
    float fCvEndOut = 0;
};

//-----------------------------------------------------
// Module Definition
//
//-----------------------------------------------------
struct ARP700 : Module
{
    enum ParamIds
    {
        nPARAMS
    };

    enum InputIds
    {
        IN_CLOCK_TRIG,
        IN_VOCT_OFF,
        IN_PROG_CHANGE,
        IN_CLOCK_RESET,
        nINPUTS
    };

    enum OutputIds
    {
        OUT_TRIG,
        OUT_VOCTS,
        nOUTPUTS
    };

    enum NoteStates
    {
        STATE_NOTE_OFF,
        STATE_NOTE_ON,
        STATE_NOTE_REST,
        STATE_TRIG_OFF
    };

    // Contructor
    ARP700()
    {
        config(nPARAMS, nINPUTS, nOUTPUTS);
        m_Clock.fsynclen = 2.0 * 48.0; // default to 120bpm
        m_Clock.IgnoreClockCount = 2;
        for (int i = 0; i < 37; i++)
            m_fKeyNotes[i] = (float)i * SEMI;

        configInput(IN_CLOCK_TRIG, "Clock");
        configInput(IN_VOCT_OFF, "V/Oct Offset");
        configInput(IN_PROG_CHANGE, "Pattern Advance Trigger");
        configInput(IN_CLOCK_RESET, "Clock Reset");
        configOutput(OUT_TRIG, "Trigger");
        configOutput(OUT_VOCTS, "V/Oct");
    }

    // pattern
    ARP_PATTERN_STRUCT m_PatternSave[MAX_ARP_PATTERNS] = {};
    PAT_STEP_STRUCT m_PatCtrl = {};

    dsp::SchmittTrigger m_SchTrigPatternChange;
    bool m_bCopySrc = false;

    std::atomic<bool> m_refreshWidgets{false};

    // clock
    dsp::SchmittTrigger m_SchTrigClk;
    MAIN_SYNC_CLOCK m_Clock;

    // global triggers
    dsp::SchmittTrigger m_SchTrigGlobalClkReset;
    bool m_GlobalClkResetPending = false;

    // keyboard
    float m_fKeyNotes[37];
    float m_VoctOffsetIn = 0;
    std::atomic<int> m_kbHighlight{-1};
    std::atomic<int> m_iPendingPattern{-1};

    // octave

    // pause
    bool m_bPauseState = false;

    int m_currStep{-1}, m_currSubstep{-1};

    // Overrides
    void JsonParams(bool bTo, json_t *root);
    void process(const ProcessArgs &args) override;
    json_t *dataToJson() override;
    void dataFromJson(json_t *rootJ) override;
    void onReset() override;

    void SetPatternSteps(int nSteps);
    void SetOut(void);
    void ChangePattern(int index, bool bForce);
    void SetPendingPattern(int phrase);
    void ArpStep(bool bReset);
    void Copy(bool bOn);
};

//-----------------------------------------------------
// Procedure:   ARP700_ModeSelect
//-----------------------------------------------------
void ARP700_ModeSelect(void *pClass, int id, int nbutton, bool bOn)
{
    ARP700 *mymodule;
    mymodule = (ARP700 *)pClass;
    mymodule->m_PatternSave[mymodule->m_PatCtrl.pat].mode = nbutton;
}

//-----------------------------------------------------
// Procedure:   ARP700_NoteOnOff
//-----------------------------------------------------
void ARP700_NoteOnOff(void *pClass, int id, int nbutton, bool bOn)
{
    int note, param;
    ARP700 *mymodule;
    mymodule = (ARP700 *)pClass;

    note = id / SUBSTEP_PER_NOTE;
    param = id - (SUBSTEP_PER_NOTE * note);
    mymodule->m_PatternSave[mymodule->m_PatCtrl.pat].onoffsel[note][param] = nbutton;
}

//-----------------------------------------------------
// Procedure:   ARP700_NoteLenSelect
//-----------------------------------------------------
void ARP700_NoteLenSelect(void *pClass, int id, int nbutton, bool bOn)
{
    int note, param;
    ARP700 *mymodule;
    mymodule = (ARP700 *)pClass;

    note = id / SUBSTEP_PER_NOTE;
    param = id - (SUBSTEP_PER_NOTE * note);
    mymodule->m_PatternSave[mymodule->m_PatCtrl.pat].lensel[note][param] = nbutton;
}

//-----------------------------------------------------
// Procedure:   ARP700_OctSelect
//-----------------------------------------------------
void ARP700_OctSelect(void *pClass, int id, int nbutton, bool bOn)
{
    ARP700 *mymodule;
    mymodule = (ARP700 *)pClass;

    mymodule->m_PatternSave[mymodule->m_PatCtrl.pat].oct = nbutton;
}

//-----------------------------------------------------
// Procedure:   ARP700_mod
//-----------------------------------------------------
void ARP700_mod(void *pClass, int id, int nbutton, bool bOn)
{
    int note, param;
    ARP700 *mymodule;
    mymodule = (ARP700 *)pClass;

    note = id / SUBSTEP_PER_NOTE;
    param = id - (SUBSTEP_PER_NOTE * note);
    mymodule->m_PatternSave[mymodule->m_PatCtrl.pat].lenmod[note][param] = nbutton;
}

//-----------------------------------------------------
// Procedure:   ARP700_Pause
//-----------------------------------------------------
void ARP700_Pause(void *pClass, int id, bool bOn)
{
    ARP700 *mymodule;
    mymodule = (ARP700 *)pClass;
    mymodule->m_bPauseState = bOn;
}

//-----------------------------------------------------
// Procedure:   ARP700_Copy
//-----------------------------------------------------
void ARP700_Copy(void *pClass, int id, bool bOn)
{
    ARP700 *mymodule;
    mymodule = (ARP700 *)pClass;
    mymodule->Copy(bOn);
}

//-----------------------------------------------------
// Procedure:   ARP700_Glide
//-----------------------------------------------------
void ARP700_Glide(void *pClass, int id, bool bOn)
{
    ARP700 *mymodule;
    mymodule = (ARP700 *)pClass;
    mymodule->m_PatternSave[mymodule->m_PatCtrl.pat].glide[id] = bOn;
    // mymodule->lg.f("Glide[ %d ] = %d\n", id, bOn );
}

//-----------------------------------------------------
// Procedure:   ARP700_Trig
//-----------------------------------------------------
void ARP700_Trig(void *pClass, int id, bool bOn)
{
    ARP700 *mymodule;
    mymodule = (ARP700 *)pClass;
    mymodule->m_PatternSave[mymodule->m_PatCtrl.pat].legato[id] = bOn;
    // mymodule->lg.f("Legato[ %d ] = %d\n", id, bOn );
}

//-----------------------------------------------------
// Procedure:   NoteChangeCallback
//
//-----------------------------------------------------
void ARP700_Widget_NoteChangeCallback(void *pClass, int kb, int notepressed, int *pnotes, bool bOn,
                                      int button, int mod)
{
    ARP700 *mymodule = (ARP700 *)pClass;

    if (!pClass)
        return;

    memcpy(mymodule->m_PatternSave[mymodule->m_PatCtrl.pat].notes, pnotes,
           sizeof(int) * MAX_ARP_NOTES);

    int nu{0};
    for (int i = 0; i < 16; ++i)
    {
        if (pnotes[i] >= 0)
            nu++;
    }

    mymodule->m_PatternSave[mymodule->m_PatCtrl.pat].notesused = nu;
}

//-----------------------------------------------------
// Procedure:   PatternChangeCallback
//
//-----------------------------------------------------
void ARP700_Widget_PatternChangeCallback(void *pClass, int kb, int pat, int max)
{
    ARP700 *mymodule = (ARP700 *)pClass;

    if (!mymodule)
        return;

    if (mymodule->m_PatCtrl.pat != pat)
    {
        if (!mymodule->m_bPauseState && mymodule->inputs[ARP700::IN_CLOCK_TRIG].isConnected())
            mymodule->SetPendingPattern(pat);
        else
            mymodule->ChangePattern(pat, false);
    }
    else if (mymodule->m_PatCtrl.used != max)
        mymodule->SetPatternSteps(max);
}

//-----------------------------------------------------
// Procedure:   Widget
//
//-----------------------------------------------------

struct ARP700_Widget : ModuleWidget
{
    PatternSelectStrip *m_pPatternSelect = NULL;

    // pattern buttons
    MyLEDButtonStrip *m_pButtonOnOff[MAX_ARP_NOTES][SUBSTEP_PER_NOTE] = {};
    MyLEDButtonStrip *m_pButtonLen[MAX_ARP_NOTES][SUBSTEP_PER_NOTE] = {};
    MyLEDButtonStrip *m_pButtonLenMod[MAX_ARP_NOTES][SUBSTEP_PER_NOTE] = {};
    MyLEDButton *m_pButtonGlide[MAX_ARP_NOTES] = {};
    MyLEDButton *m_pButtonTrig[MAX_ARP_NOTES] = {};
    MyLEDButtonStrip *m_plastbut = NULL;
    MyLEDButton *m_pButtonCopy = NULL;

    Keyboard_3Oct_Widget *pKeyboardWidget = NULL;
    MyLEDButtonStrip *m_pButtonOctaveSelect = NULL;
    MyLEDButton *m_pButtonPause = 0;
    MyLEDButtonStrip *m_pButtonMode = 0;

    ARP700_Widget(ARP700 *module)
    {
        int x, y, note, param;
        // box.size = Vec( 15*27, 380);

        setModule(module);

        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ARP700.svg")));

        // screw
        addChild(createWidget<ScrewSilver>(Vec(15, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 0)));
        addChild(createWidget<ScrewSilver>(Vec(15, 365)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 365)));

        // module->lg.Open("ARP700.txt");

        // pause button
        m_pButtonPause =
            new MyLEDButton(75, 22, 11, 11, 8.0, DWRGB(180, 180, 180), DWRGB(255, 0, 0),
                            MyLEDButton::TYPE_SWITCH, 0, module, ARP700_Pause);
        addChild(m_pButtonPause);

        // copy button
        m_pButtonCopy =
            new MyLEDButton(307, 22, 11, 11, 8.0, DWRGB(180, 180, 180), DWRGB(0, 255, 255),
                            MyLEDButton::TYPE_SWITCH, 0, module, ARP700_Copy);
        addChild(m_pButtonCopy);

        // keyboard widget
        pKeyboardWidget = new Keyboard_3Oct_Widget(75, 38, MAX_ARP_NOTES, 0, DWRGB(255, 128, 64),
                                                   module, ARP700_Widget_NoteChangeCallback);
        addChild(pKeyboardWidget);

        // octave select
        m_pButtonOctaveSelect = new MyLEDButtonStrip(
            307, 104, 11, 11, 3, 8.0, 4, false, DWRGB(180, 180, 180), DWRGB(0, 255, 255),
            MyLEDButtonStrip::TYPE_EXCLUSIVE, 0, module, ARP700_OctSelect);
        addChild(m_pButtonOctaveSelect);

        // pattern selects
        m_pPatternSelect = new PatternSelectStrip(
            75, 104, 9, 7, DWRGB(200, 200, 200), DWRGB(40, 40, 40), DWRGB(112, 104, 102),
            DWRGB(40, 40, 40), MAX_ARP_PATTERNS, 0, module, ARP700_Widget_PatternChangeCallback);
        addChild(m_pPatternSelect);

        x = 60;

        for (note = 0; note < MAX_ARP_NOTES; note++)
        {
            for (param = 0; param < SUBSTEP_PER_NOTE; param++)
            {
                y = 140;

                m_pButtonOnOff[note][param] = new MyLEDButtonStrip(
                    x, y, 12, 12, 2, 10.0, 3, true, DWRGB(180, 180, 180), DWRGB(255, 0, 0),
                    MyLEDButtonStrip::TYPE_EXCLUSIVE, (note * SUBSTEP_PER_NOTE) + param, module,
                    ARP700_NoteOnOff);
                addChild(m_pButtonOnOff[note][param]);

                m_pButtonOnOff[note][param]->SetLEDCol(1, DWRGB(0, 255, 0));
                m_pButtonOnOff[note][param]->SetLEDCol(2, DWRGB(255, 255, 0));

                y += 43;

                m_pButtonLen[note][param] = new MyLEDButtonStrip(
                    x, y, 12, 12, 2, 10.0, 6, true, DWRGB(180, 180, 180), DWRGB(255, 128, 0),
                    MyLEDButtonStrip::TYPE_EXCLUSIVE, (note * SUBSTEP_PER_NOTE) + param, module,
                    ARP700_NoteLenSelect);
                addChild(m_pButtonLen[note][param]);

                y += 89;

                m_pButtonLenMod[note][param] =
                    new MyLEDButtonStrip(x, y, 12, 12, 2, 10.0, 3, true, DWRGB(180, 180, 180),
                                         DWRGB(255, 128, 0), MyLEDButtonStrip::TYPE_EXCLUSIVE_WOFF,
                                         (note * SUBSTEP_PER_NOTE) + param, module, ARP700_mod);
                addChild(m_pButtonLenMod[note][param]);

                if (param == 1)
                {
                    y += 43;

                    m_pButtonGlide[note] = new MyLEDButton(
                        x, y, 12, 12, 10.0, DWRGB(180, 180, 180), DWRGB(0, 255, 255),
                        MyLEDButton::TYPE_SWITCH, note, module, ARP700_Glide);
                    addChild(m_pButtonGlide[note]);

                    y += 16;

                    m_pButtonTrig[note] = new MyLEDButton(
                        x, y, 12, 12, 10.0, DWRGB(180, 180, 180), DWRGB(0, 255, 255),
                        MyLEDButton::TYPE_SWITCH, note, module, ARP700_Trig);
                    addChild(m_pButtonTrig[note]);
                }

                x += 14;
            }

            x += 5;
        }

        // clock trigger
        addInput(createInput<MyPortInSmall>(Vec(44, 21), module, ARP700::IN_CLOCK_TRIG));

        // VOCT offset input
        addInput(createInput<MyPortInSmall>(Vec(44, 52), module, ARP700::IN_VOCT_OFF));

        // prog change trigger
        addInput(createInput<MyPortInSmall>(Vec(44, 102), module, ARP700::IN_PROG_CHANGE));

        // outputs
        addOutput(createOutput<MyPortOutSmall>(Vec(365, 38), module, ARP700::OUT_VOCTS));
        addOutput(createOutput<MyPortOutSmall>(Vec(365, 79), module, ARP700::OUT_TRIG));

        // reset inputs
        addInput(createInput<MyPortInSmall>(Vec(14, 21), module, ARP700::IN_CLOCK_RESET));

        // mode buttons
        m_pButtonMode = new MyLEDButtonStrip(
            154, 360, 12, 12, 7, 10.0, 7, false, DWRGB(180, 180, 180), DWRGB(255, 255, 0),
            MyLEDButtonStrip::TYPE_EXCLUSIVE, 0, module, ARP700_ModeSelect);
        addChild(m_pButtonMode);
    }

    void step() override
    {
        auto az = dynamic_cast<ARP700 *>(module);
        if (az)
        {
            if (az->m_refreshWidgets)
            {
                az->m_refreshWidgets = false;
                m_pButtonPause->Set(az->m_bPauseState);
                pKeyboardWidget->setkey(az->m_PatternSave[az->m_PatCtrl.pat].notes);

                // This is unlikely to be correct
                az->ChangePattern(az->m_PatCtrl.pat, true);
                az->ArpStep(true);
            }

            for (auto note = 0; note < MAX_ARP_NOTES; note++)
            {
                m_pButtonGlide[note]->Set(az->m_PatternSave[az->m_PatCtrl.pat].glide[note]);
                m_pButtonTrig[note]->Set(az->m_PatternSave[az->m_PatCtrl.pat].legato[note]);

                for (auto param = 0; param < SUBSTEP_PER_NOTE; param++)
                {
                    m_pButtonOnOff[note][param]->Set(
                        az->m_PatternSave[az->m_PatCtrl.pat].onoffsel[note][param], true);
                    m_pButtonLen[note][param]->Set(
                        az->m_PatternSave[az->m_PatCtrl.pat].lensel[note][param], true);
                    m_pButtonLenMod[note][param]->Set(
                        az->m_PatternSave[az->m_PatCtrl.pat].lenmod[note][param], true);
                }
            }
            m_pButtonOctaveSelect->Set(az->m_PatternSave[az->m_PatCtrl.pat].oct, true);
            m_pButtonMode->Set(az->m_PatternSave[az->m_PatCtrl.pat].mode, true);
            m_pPatternSelect->SetPat(az->m_PatCtrl.pat, false);
            m_pPatternSelect->SetMax(az->m_PatCtrl.used);

            // set keyboard keys
            pKeyboardWidget->setkey(az->m_PatternSave[az->m_PatCtrl.pat].notes);
            if (az->m_kbHighlight >= 0)
                pKeyboardWidget->setkeyhighlight(az->m_kbHighlight);

            if (m_plastbut)
                m_plastbut->SetHiLightOn(-1);

            auto nstep = az->m_currStep;
            auto substep = az->m_currSubstep;
            if (nstep >= 0 && substep >= 0)
            {
                m_pButtonLen[nstep][substep]->SetHiLightOn(
                    az->m_PatternSave[az->m_PatCtrl.pat].lensel[nstep][substep]);
                m_plastbut = m_pButtonLen[nstep][substep];
            }

            if (az->m_iPendingPattern >= 0)
            {
                m_pPatternSelect->SetPat(az->m_iPendingPattern, true);
            }
            m_pButtonCopy->Set(az->m_bCopySrc);
        }
        Widget::step();
    }
};

//-----------------------------------------------------
// Procedure: JsonParams
//
//-----------------------------------------------------
void ARP700::JsonParams(bool bTo, json_t *root)
{
    JsonDataBool(bTo, "m_bPauseState", root, &m_bPauseState, 1);
    JsonDataInt(bTo, "m_CurrentPattern", root, &m_PatCtrl.pat, 1);
    JsonDataInt(bTo, "m_PatternSave", root, (int *)m_PatternSave, sizeof(m_PatternSave) / 4);
    JsonDataInt(bTo, "m_PatternsUsed", root, &m_PatCtrl.used, 1);
}

//-----------------------------------------------------
// Procedure: toJson
//
//-----------------------------------------------------
json_t *ARP700::dataToJson()
{
    json_t *root = json_object();

    if (!root)
        return NULL;

    JsonParams(TOJSON, root);

    return root;
}

//-----------------------------------------------------
// Procedure:   fromJson
//
//-----------------------------------------------------
void ARP700::dataFromJson(json_t *root)
{
    JsonParams(FROMJSON, root);

    m_refreshWidgets = true;
}

//-----------------------------------------------------
// Procedure:   reset
//
//-----------------------------------------------------
void ARP700::onReset()
{
    int pat, note;

    m_PatCtrl.fCvStartOut = 0;
    m_PatCtrl.fCvEndOut = 0;
    memset(m_PatternSave, 0, sizeof(m_PatternSave));

    for (pat = 0; pat < MAX_ARP_PATTERNS; pat++)
    {
        for (note = 0; note < MAX_ARP_NOTES; note++)
            m_PatternSave[pat].notes[note] = -1;
    }

    SetPatternSteps(MAX_ARP_PATTERNS - 1);
    ChangePattern(0, true);
}

//-----------------------------------------------------
// Procedure:   SetPhraseSteps
//
//-----------------------------------------------------
void ARP700::SetPatternSteps(int nSteps)
{
    if (nSteps < 0 || nSteps >= MAX_ARP_PATTERNS)
        nSteps = 0;

    m_PatCtrl.used = nSteps;
}

//-----------------------------------------------------
// Procedure:   SetOut
//
//-----------------------------------------------------
void ARP700::SetOut(void)
{
    int note = 0, nstep, substep;
    float foct;

    m_VoctOffsetIn = inputs[IN_VOCT_OFF].getNormalVoltage(0.0);

    nstep = m_PatCtrl.step / SUBSTEP_PER_NOTE;
    substep = m_PatCtrl.step - (nstep * SUBSTEP_PER_NOTE);

    if (m_PatternSave[m_PatCtrl.pat].onoffsel[nstep][substep] == ARP_ON)
    {
        note = m_PatternSave[m_PatCtrl.pat].notes[nstep];
        m_kbHighlight = note;
    }
    else
        return;

    if (note > 36 || note < 0)
        note = 0;

    foct = (float)m_PatternSave[m_PatCtrl.pat].oct;

    m_PatCtrl.fCvEndOut = foct + m_fKeyNotes[note] + m_VoctOffsetIn;

    // start glide note (last pattern note)
    if (m_PatCtrl.bWasLastNotePlayed)
    {
        m_PatCtrl.fCvStartOut = m_PatCtrl.fLastNotePlayed + m_VoctOffsetIn;
    }
    else
    {
        m_PatCtrl.bWasLastNotePlayed = true;
        m_PatCtrl.fCvStartOut = m_PatCtrl.fCvEndOut + m_VoctOffsetIn;
    }

    m_PatCtrl.fLastNotePlayed = m_PatCtrl.fCvEndOut + m_VoctOffsetIn;

    if (m_PatternSave[m_PatCtrl.pat].glide[nstep])
    {
        // glide time
        m_PatCtrl.glideCount = 0.2 * APP->engine->getSampleRate();
        m_PatCtrl.fglideInc = 1.0 / (float)m_PatCtrl.glideCount;

        m_PatCtrl.fglide = 1.0;
    }
    else
    {
        m_PatCtrl.fglide = 0.0;
        m_PatCtrl.glideCount = 0;
    }
}

//-----------------------------------------------------
// Procedure:   SetPendingPattern
//
//-----------------------------------------------------
void ARP700::SetPendingPattern(int patin)
{
    int pattern;

    if (patin < 0 || patin >= MAX_ARP_PATTERNS)
        pattern = (m_PatCtrl.pat + 1) & 0x7;
    else
        pattern = patin;

    if (pattern > m_PatCtrl.used)
        pattern = 0;

    m_PatCtrl.pending.bPending = true;
    m_PatCtrl.pending.pat = pattern;
    m_iPendingPattern = pattern;
}

//-----------------------------------------------------
// Procedure:   Copy
//
//-----------------------------------------------------
void ARP700::Copy(bool bOn)
{
    if (!m_bPauseState || !bOn)
    {
        m_bCopySrc = false;
    }
    else if (bOn)
    {
        m_bCopySrc = true;
    }
}

//-----------------------------------------------------
// Procedure:   ChangePattern
//
//-----------------------------------------------------
void ARP700::ChangePattern(int index, bool bForce)
{
    if (!bForce && index == m_PatCtrl.pat)
        return;

    if (index < 0)
        index = MAX_ARP_PATTERNS - 1;
    else if (index >= MAX_ARP_PATTERNS)
        index = 0;

    if (m_bCopySrc)
    {
        // do not copy if we are not paused
        if (m_bPauseState)
        {
            memcpy(&m_PatternSave[index], &m_PatternSave[m_PatCtrl.pat],
                   sizeof(ARP_PATTERN_STRUCT));
            m_bCopySrc = false;
        }
    }

    m_PatCtrl.pat = index;
}

//-----------------------------------------------------
// Procedure:   IncStep
//
//-----------------------------------------------------
#define BASE_TICK_16th 48 // for 16th note

const float fbasenotelen[6] = {BASE_TICK_16th * 4, BASE_TICK_16th * 2, BASE_TICK_16th,
                               BASE_TICK_16th / 2, BASE_TICK_16th / 4, BASE_TICK_16th / 8};

const int patmode[7][42] = {
    {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
     19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0,  -1},
    {20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
     1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, -1},
    {20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20},
    {0,  20, 1, 19, 2, 18, 3, 17, 4, 16, 5, 15, 6, 14, 7, 13, 8, 12, 9, 11, 10,
     10, 11, 9, 12, 8, 13, 7, 14, 6, 15, 5, 16, 4, 17, 3, 18, 2, 19, 1, 20, 0},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};

void ARP700::ArpStep(bool bReset)
{
    int i, nstep, substep, modestp;

    if (!m_PatternSave[m_PatCtrl.pat].notesused)
    {
        m_PatCtrl.virtstep = -1;
        m_PatCtrl.bTrig = false;
        return;
    }
    else
    {
        if (bReset)
            m_PatCtrl.virtstep = -1;

        // find next used step
        for (i = 0; i <= (SUBSTEP_PER_NOTE * MAX_ARP_NOTES * 2); i++)
        {
            m_PatCtrl.virtstep++;

            if (m_PatCtrl.virtstep >= (SUBSTEP_PER_NOTE * MAX_ARP_NOTES * 2))
                m_PatCtrl.virtstep = 0;

            if (m_PatternSave[m_PatCtrl.pat].mode == 6)
                modestp = (int)(random::uniform() * 20.0f);
            else
                modestp = patmode[m_PatternSave[m_PatCtrl.pat].mode][m_PatCtrl.virtstep];

            if (modestp != -1)
            {
                nstep = modestp / SUBSTEP_PER_NOTE;
                substep = modestp - (nstep * SUBSTEP_PER_NOTE);

                if (m_PatternSave[m_PatCtrl.pat].onoffsel[nstep][substep] != ARP_OFF)
                {
                    m_PatCtrl.step = modestp;
                    goto stepfound;
                }
            }
            else
            {
                m_PatCtrl.virtstep = -1;
            }
        }
    }

    // if we are here, no step was found
    m_PatCtrl.step = -1;
    m_PatCtrl.bTrig = false;
    return;

stepfound:

    if (m_PatCtrl.step == 0)
    {
        if (m_PatCtrl.pending.bPending)
        {
            m_PatCtrl.pending.bPending = false;
            m_iPendingPattern = -1;
            ChangePattern(m_PatCtrl.pending.pat, true);
        }
    }

    nstep = m_PatCtrl.step / SUBSTEP_PER_NOTE;
    substep = m_PatCtrl.step - (nstep * SUBSTEP_PER_NOTE);

    m_currStep = nstep;
    m_currSubstep = substep;

    // next step length
    m_PatCtrl.nextcount = fbasenotelen[m_PatternSave[m_PatCtrl.pat].lensel[nstep][substep]];

    switch (m_PatternSave[m_PatCtrl.pat].lenmod[nstep][substep])
    {
    case 1: // x2
        m_PatCtrl.nextcount *= 2;
        break;
    case 2: // dotted
        m_PatCtrl.nextcount += m_PatCtrl.nextcount / 2;
        break;
    case 3: // triplet
        m_PatCtrl.nextcount = m_PatCtrl.nextcount / 3;
        break;
    }

    // if this is note on return true
    if (m_PatternSave[m_PatCtrl.pat].onoffsel[nstep][substep] == ARP_ON)
    {
        SetOut();

        if (m_PatternSave[m_PatCtrl.pat].legato[nstep])
            m_PatCtrl.bTrig = false;
        else
            m_PatCtrl.bTrig = true;
    }
}

//-----------------------------------------------------
// Procedure:   step
//
//-----------------------------------------------------
void ARP700::process(const ProcessArgs &args)
{
    bool bSyncTick = false, bResetClk = false;

    if (!inputs[IN_CLOCK_TRIG].isConnected())
    {
        outputs[OUT_TRIG].setVoltage(0.0f);
        m_Clock.tickcount = 0;
        m_Clock.fsynclen = 2.0 * 48.0; // default to 120bpm
        m_Clock.fsynccount = 0;
        m_Clock.IgnoreClockCount = 2;

        if (m_PatCtrl.pending.bPending)
        {
            m_PatCtrl.pending.bPending = false;
            m_iPendingPattern = -1;
            ChangePattern(m_PatCtrl.pending.pat, false);
        }

        return;
    }

    // global clock reset
    m_Clock.bClockReset =
        (m_SchTrigGlobalClkReset.process(inputs[IN_CLOCK_RESET].getNormalVoltage(0.0f)));

    // pattern change trig
    if (!m_bPauseState &&
        m_SchTrigPatternChange.process(inputs[IN_PROG_CHANGE].getNormalVoltage(0.0f)))
        SetPendingPattern(-1);

    // track clock period
    if (m_SchTrigClk.process(inputs[IN_CLOCK_TRIG].getNormalVoltage(0.0f)) || m_Clock.bClockReset)
    {
        if (m_Clock.bClockReset)
        {
            m_Clock.tickcount = 0;
            // ArpStep( true );
            m_Clock.bClockReset = false;
            m_Clock.IgnoreClockCount = 2;
            bResetClk = true;
        }

        if (m_Clock.tickcount && --m_Clock.IgnoreClockCount <= 0)
        {
            m_Clock.IgnoreClockCount = 0;
            m_Clock.fsynclen =
                (float)(APP->engine->getSampleRate() / (float)m_Clock.tickcount) * 48.0;
        }

        m_Clock.fsynccount = 0;
        bSyncTick = true;
        m_Clock.tickcount = 0;
    }
    else
    {
        // keep track of sync tick (16th / 12 )
        m_Clock.fsynccount += m_Clock.fsynclen;
        if (m_Clock.fsynccount >= APP->engine->getSampleRate())
        {
            m_Clock.fsynccount = m_Clock.fsynccount - APP->engine->getSampleRate();
            bSyncTick = true;
        }
    }

    m_Clock.tickcount++;

    if (m_bPauseState)
    {
        m_PatCtrl.bTrig = false;

        if (m_PatCtrl.pending.bPending)
        {
            m_PatCtrl.pending.bPending = false;
            m_iPendingPattern = -1;
            ChangePattern(m_PatCtrl.pending.pat, false);
        }
    }
    else if (bSyncTick)
    {
        if (bResetClk)
            m_PatCtrl.nextcount = 0;
        else
            m_PatCtrl.nextcount--;

        // cut off note one tick early so they don't legato
        if (m_PatCtrl.nextcount == 1)
            m_PatCtrl.bTrig = false;

        else if (m_PatCtrl.nextcount <= 0)
        {
            ArpStep(bResetClk);
        }
    }

    outputs[OUT_TRIG].setVoltage(m_PatCtrl.bTrig ? CV_MAX10 : 0.0);

    if (--m_PatCtrl.glideCount > 0)
        m_PatCtrl.fglide -= m_PatCtrl.fglideInc;
    else
        m_PatCtrl.fglide = 0.0;

    outputs[OUT_VOCTS].setVoltage((m_PatCtrl.fCvStartOut * m_PatCtrl.fglide) +
                                  (m_PatCtrl.fCvEndOut * (1.0 - m_PatCtrl.fglide)));
}

Model *modelARP700 = createModel<ARP700, ARP700_Widget>("ARP700");
