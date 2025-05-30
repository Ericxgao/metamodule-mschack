#pragma once

#define QITEMS 256

struct LINE_Q
{
    LINE_Q()
    {
        for (auto i = 0U; i < QITEMS; ++i)
            item[i] = rack::Vec();
    }

    Vec item[QITEMS];
    unsigned char index{0};
};

//-----------------------------------------------------
// Widget_Image
//-----------------------------------------------------
struct Widget_LineImage : TransparentWidget
{
    bool m_bInitialized = false;

    LINE_Q m_Q;

    RGB_STRUCT m_BGCol = {};

    //-----------------------------------------------------
    // Procedure:   constructor
    //-----------------------------------------------------
    Widget_LineImage(float x, float y, float w, float h) : m_Q{}
    {
        box.pos = Vec(x, y);
        box.size = Vec(w, h);

        m_bInitialized = true;
    }

    //-----------------------------------------------------
    // Procedure:   SetBGCol
    //-----------------------------------------------------
    void SetBGCol(int bg) { m_BGCol.dwCol = bg; }

    //-----------------------------------------------------
    // Procedure:   addQ
    //-----------------------------------------------------
    bool addQ(float x, float y)
    {
        if (!m_bInitialized)
            return false;

        m_Q.item[m_Q.index].x = x;
        m_Q.item[m_Q.index++].y = y;

        return true;
    }

    void draw(const DrawArgs &args) override
    {
        nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
        // nvgShapeAntiAlias( args.vg, 1);

        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x - 1, box.size.y - 1);
        nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 0x80));
        nvgFill(args.vg);
    }

    //-----------------------------------------------------
    // Procedure:   draw
    //-----------------------------------------------------
    void drawLayer(const DrawArgs &args, int layer) override
    {
        if (layer != 1)
            return;

        nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
        Vec *pv; //, *last, *next, dl, dn;
        unsigned char index;

        // nvgBezierTo(vg, sx[i-1]+dx*0.5f,sy[i-1], sx[i]-dx*0.5f,sy[i], sx[i],sy[i]);

        if (!m_bInitialized)
            return;

        nvgStrokeWidth(args.vg, 2.5f);
        nvgStrokeColor(args.vg, nvgRGBA(0, 255, 255, 250));

        index = m_Q.index + 1;

        pv = &m_Q.item[index++];
        // last = pv;

        nvgLineJoin(args.vg, NVG_ROUND);

        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, pv->x, pv->y);

        // pv =&m_Q.item[ index++ ];

        for (int i = 0; i < 255; i++)
        {
            pv = &m_Q.item[index++];
            nvgLineTo(args.vg, pv->x, pv->y);
        }

        nvgStroke(args.vg);
    }
};