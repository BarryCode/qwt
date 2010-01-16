/* -*- mode: C++ ; c-file-style: "stroustrup" -*- *****************************
 * Qwt Widget Library
 * Copyright (C) 1997   Josef Wilgen
 * Copyright (C) 2002   Uwe Rathmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the Qwt License, Version 1.0
 *****************************************************************************/

#include "qwt_plot.h"
#include "qwt_painter.h"
#include "qwt_legend_item.h"
#include "qwt_plot_canvas.h"
#include "qwt_plot_layout.h"
#include "qwt_legend.h"
#include "qwt_dyngrid_layout.h"
#include "qwt_scale_widget.h"
#include "qwt_scale_engine.h"
#include "qwt_text.h"
#include "qwt_text_label.h"
#include "qwt_math.h"
#include <qpainter.h>
#include <qpaintengine.h>
#include <qtransform.h>

/*!
  \brief Print the plot to a \c QPaintDevice (\c QPrinter)
  This function prints the contents of a QwtPlot instance to
  \c QPaintDevice object. The size is derived from its device
  metrics.

  \param paintDev device to paint on, often a printer
  \param pfilter print filter

  \sa QwtPlotPrintFilter
*/

void QwtPlot::print(QPaintDevice &paintDev,
   const QwtPlotPrintFilter &pfilter) const
{
    int w = paintDev.width();
    int h = paintDev.height();

    QRect rect(0, 0, w, h);
    double aspect = double(rect.width())/double(rect.height());
    if ((aspect < 1.0))
        rect.setHeight(int(aspect*rect.width()));

    QPainter p(&paintDev);
    print(&p, rect, pfilter);
}

/*!
  \brief Paint the plot into a given rectangle.
  Paint the contents of a QwtPlot instance into a given rectangle.

  \param painter Painter
  \param plotRect Bounding rectangle
  \param pfilter Print filter

  \sa QwtPlotPrintFilter
*/
void QwtPlot::print(QPainter *painter, const QRectF &plotRect,
        const QwtPlotPrintFilter &pfilter) const
{
    int axisId;

    if ( painter == 0 || !painter->isActive() ||
            !plotRect.isValid() || size().isNull() )
       return;

    /* 
      The layout engine uses the same methods as they are used
      by the Qt layout system. Therefore we need to calculate the 
      layout in screen coordinates and paint with a scaled painter.
     */
    QTransform transform;
    transform.scale(
        double(painter->device()->logicalDpiX()) / logicalDpiX(),
        double(painter->device()->logicalDpiY()) / logicalDpiY() );
        
    painter->save();

    pfilter.apply((QwtPlot *)this, transform.isScaling());

    int baseLineDists[QwtPlot::axisCnt];
    if ( pfilter.options() & QwtPlotPrintFilter::PrintFrameWithScales )
    {
        for (axisId = 0; axisId < QwtPlot::axisCnt; axisId++ )
        {
            QwtScaleWidget *scaleWidget = (QwtScaleWidget *)axisWidget(axisId);
            if ( scaleWidget )
            {
                baseLineDists[axisId] = scaleWidget->margin();
                scaleWidget->setMargin(0);
            }
        }
    }
    // Calculate the layout for the print.

    int layoutOptions = QwtPlotLayout::IgnoreScrollbars 
        | QwtPlotLayout::IgnoreFrames;
    if ( !(pfilter.options() & QwtPlotPrintFilter::PrintMargin) )
        layoutOptions |= QwtPlotLayout::IgnoreMargin;
    if ( !(pfilter.options() & QwtPlotPrintFilter::PrintLegend) )
        layoutOptions |= QwtPlotLayout::IgnoreLegend;

    const QRectF layoutRect = transform.inverted().mapRect(plotRect);
    ((QwtPlot *)this)->plotLayout()->activate(this, 
        layoutRect, layoutOptions);

    painter->setTransform(transform);

    if ((pfilter.options() & QwtPlotPrintFilter::PrintTitle)
        && (!titleLabel()->text().isEmpty()))
    {
        printTitle(painter, plotLayout()->titleRect());
    }

    if ( (pfilter.options() & QwtPlotPrintFilter::PrintLegend)
        && legend() && !legend()->isEmpty() )
    {
        printLegend(painter, plotLayout()->legendRect());
    }

    for ( axisId = 0; axisId < QwtPlot::axisCnt; axisId++ )
    {
        QwtScaleWidget *scaleWidget = (QwtScaleWidget *)axisWidget(axisId);
        if (scaleWidget)
        {
            int baseDist = scaleWidget->margin();

            int startDist, endDist;
            scaleWidget->getBorderDistHint(startDist, endDist);

            printScale(painter, axisId, startDist, endDist,
                baseDist, plotLayout()->scaleRect(axisId));
        }
    }

    QRectF canvasRect = plotLayout()->canvasRect();

    QwtScaleMap map[axisCnt];
    for (axisId = 0; axisId < axisCnt; axisId++)
    {
        map[axisId].setTransformation(axisScaleEngine(axisId)->transformation());

        const QwtScaleDiv &scaleDiv = *axisScaleDiv(axisId);
        map[axisId].setScaleInterval(
            scaleDiv.lowerBound(), scaleDiv.upperBound());

        double from, to;
        if ( axisEnabled(axisId) )
        {
            const int sDist = axisWidget(axisId)->startBorderDist();
            const int eDist = axisWidget(axisId)->endBorderDist();
            const QRectF &scaleRect = plotLayout()->scaleRect(axisId);

            if ( axisId == xTop || axisId == xBottom )
            {
                from = scaleRect.left() + sDist;
                to = scaleRect.right() - eDist;
            }
            else
            {
                from = scaleRect.bottom() - eDist;
                to = scaleRect.top() + sDist;
            }
        }
        else
        {
            int margin = plotLayout()->canvasMargin(axisId);
            if ( axisId == yLeft || axisId == yRight )
            {
                from = canvasRect.bottom() - 1 - margin;
                to = canvasRect.top() + margin;
            }
            else
            {
                from = canvasRect.left() + margin;
                to = canvasRect.right() - 1 - margin;
            }
        }
        map[axisId].setPaintInterval(from, to);
    }

    /*
      While we can scale vector graphics we can't do the same for
      raster data. So we better scale the canvas rectangle back to 
      device resolution and render in the resolution of the paint device.
     */
    painter->resetTransform();
    canvasRect = transform.mapRect(canvasRect);
    for (axisId = 0; axisId < axisCnt; axisId++)
    {
        double p1 = map[axisId].p1();
        double p2 = map[axisId].p2();

        if ( axisId == xTop || axisId == xBottom )
        {
            p1 *= transform.m11();
            p2 *= transform.m11();
        }
        else
        {
            p1 *= transform.m22();
            p2 *= transform.m22();
        }

        map[axisId].setPaintInterval( p1, p2 ); 
    }

    // canvas 
    printCanvas(painter, canvasRect, map, pfilter);

    ((QwtPlot *)this)->plotLayout()->invalidate();

    // reset all widgets with their original attributes.
    if ( pfilter.options() & QwtPlotPrintFilter::PrintFrameWithScales )
    {
        // restore the previous base line dists

        for (axisId = 0; axisId < QwtPlot::axisCnt; axisId++ )
        {
            QwtScaleWidget *scaleWidget = (QwtScaleWidget *)axisWidget(axisId);
            if ( scaleWidget  )
                scaleWidget->setMargin(baseLineDists[axisId]);
        }
    }

    pfilter.reset((QwtPlot *)this);

    painter->restore();
}

/*!
  Print the title into a given rectangle.

  \param painter Painter
  \param rect Bounding rectangle
*/

void QwtPlot::printTitle(QPainter *painter, const QRectF &rect) const
{
    painter->setFont(titleLabel()->font());

    const QColor color = titleLabel()->palette().color(
            QPalette::Active, QPalette::Text);

    painter->setPen(color);
    titleLabel()->text().draw(painter, rect);
}

/*!
  Print the legend into a given rectangle.

  \param painter Painter
  \param rect Bounding rectangle
*/

void QwtPlot::printLegend(QPainter *painter, const QRectF &rect) const
{
    if ( !legend() || legend()->isEmpty() )
        return;

    QLayout *l = legend()->contentsWidget()->layout();
    if ( l == 0 || !l->inherits("QwtDynGridLayout") )
        return;

    QwtDynGridLayout *legendLayout = (QwtDynGridLayout *)l;

    uint numCols = legendLayout->columnsForWidth(rect.width());
    QList<QRect> itemRects = 
        legendLayout->layoutItems(rect.toRect(), numCols);

    int index = 0;

    for ( int i = 0; i < legendLayout->count(); i++ )
    {
        QLayoutItem *item = legendLayout->itemAt(i);
        QWidget *w = item->widget();
        if ( w )
        {
            painter->save();
            painter->setClipping(true);
            QwtPainter::setClipRect(painter, itemRects[index]);

            printLegendItem(painter, w, itemRects[index]);

            index++;
            painter->restore();
        }
    }
}

/*!
  Print the legend item into a given rectangle.

  \param painter Painter
  \param w Widget representing a legend item
  \param rect Bounding rectangle
*/

void QwtPlot::printLegendItem(QPainter *painter, 
    const QWidget *w, const QRectF &rect) const
{
    if ( w->inherits("QwtLegendItem") )
    {
        QwtLegendItem *item = (QwtLegendItem *)w;

        const QRect identifierRect(
            rect.x() + item->margin(), rect.y(),
            item->identifierWidth(), rect.height());

        QwtLegendItemManager *itemManger = legend()->find(item);
        if ( itemManger )
        {
            painter->save();
            itemManger->drawLegendIdentifier(painter, identifierRect);
            painter->restore();
        }

        // Label
    
        QRectF titleRect = rect;
        titleRect.setX(identifierRect.right() + 2 * item->spacing());

        painter->setFont(item->font());
        item->text().draw(painter, titleRect);
    }
}

/*!
  \brief Paint a scale into a given rectangle.
  Paint the scale into a given rectangle.

  \param painter Painter
  \param axisId Axis
  \param startDist Start border distance
  \param endDist End border distance
  \param baseDist Base distance
  \param rect Bounding rectangle
*/

void QwtPlot::printScale(QPainter *painter,
    int axisId, int startDist, int endDist, int baseDist, 
    const QRectF &rect) const
{
    if (!axisEnabled(axisId))
        return;

    const QwtScaleWidget *scaleWidget = axisWidget(axisId);
    if ( scaleWidget->isColorBarEnabled() 
        && scaleWidget->colorBarWidth() > 0)
    {
        QRectF r = rect;
        r.setWidth(r.width() - 1);
        r.setHeight(r.height() - 1);

        scaleWidget->drawColorBar(painter, scaleWidget->colorBarRect(r));

        const int off = scaleWidget->colorBarWidth() + scaleWidget->spacing();
        if ( scaleWidget->scaleDraw()->orientation() == Qt::Horizontal )
            baseDist += off;
        else
            baseDist += off;
    }

    QwtScaleDraw::Alignment align;
    int x, y, w;

    switch(axisId)
    {
        case yLeft:
        {
            x = rect.right() - 1 - baseDist;
            y = rect.y() + startDist;
            w = rect.height() - startDist - endDist;
            align = QwtScaleDraw::LeftScale;
            break;
        }
        case yRight:
        {
            x = rect.left() + baseDist;
            y = rect.y() + startDist;
            w = rect.height() - startDist - endDist;
            align = QwtScaleDraw::RightScale;
            break;
        }
        case xTop:
        {
            x = rect.left() + startDist;
            y = rect.bottom() - baseDist;
            w = rect.width() - startDist - endDist;
            align = QwtScaleDraw::TopScale;
            break;
        }
        case xBottom:
        {
            x = rect.left() + startDist;
            y = rect.top() + baseDist;
            w = rect.width() - startDist - endDist;
            align = QwtScaleDraw::BottomScale;
            break;
        }
        default:
            return;
    }

    scaleWidget->drawTitle(painter, align, rect);

    painter->save();
    painter->setFont(scaleWidget->font());

    QPen pen = painter->pen();
    pen.setWidth(scaleWidget->penWidth());
    painter->setPen(pen);

    QwtScaleDraw *sd = (QwtScaleDraw *)scaleWidget->scaleDraw();
    const QPointF sdPos = sd->pos();
    const double sdLength = sd->length();

    sd->move(x, y);
    sd->setLength(w);

    QPalette palette = scaleWidget->palette();
    palette.setCurrentColorGroup(QPalette::Active);
    sd->draw(painter, palette);

    // reset previous values
    sd->move(sdPos); 
    sd->setLength(sdLength); 

    painter->restore();
}

/*!
  Print the canvas into a given rectangle.

  \param painter Painter
  \param map Maps mapping between plot and paint device coordinates
  \param canvasRect Canvas rectangle
  \param pfilter Print filter
  \sa QwtPlotPrintFilter
*/

void QwtPlot::printCanvas(QPainter *painter, const QRectF &canvasRect,
    const QwtScaleMap map[axisCnt], const QwtPlotPrintFilter &pfilter) const
{
    if ( pfilter.options() & QwtPlotPrintFilter::PrintBackground )
    {
        const QBrush bgBrush = canvas()->palette().brush(backgroundRole());
        QRectF r = canvasRect;
        if ( !(pfilter.options() & QwtPlotPrintFilter::PrintFrameWithScales) )
        {
            r = canvasRect;
            // Unfortunately the paint engines do no always the same
            const QPaintEngine *pe = painter->paintEngine();
            if ( pe )
            {
                switch(painter->paintEngine()->type() )
                {
                    case QPaintEngine::Raster:
                    case QPaintEngine::X11:
                        break;
                    default:
                        r.setWidth(r.width() - 1);
                        r.setHeight(r.height() - 1);
                        break;
                }
            }
        }
        QwtPainter::fillRect(painter, r, bgBrush);
    }

    if ( pfilter.options() & QwtPlotPrintFilter::PrintFrameWithScales )
    {
        painter->save();
        painter->setPen(QPen(Qt::black));
        painter->setBrush(QBrush(Qt::NoBrush));
        QwtPainter::drawRect(painter, canvasRect);
        painter->restore();
    }

    painter->setClipping(true);
    QwtPainter::setClipRect(painter, canvasRect.toRect());

    drawItems(painter, canvasRect, map, pfilter);
}
