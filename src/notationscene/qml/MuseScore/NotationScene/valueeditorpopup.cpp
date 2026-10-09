/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "valueeditorpopup.h"

#include <algorithm>

#include <QDoubleSpinBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QScreen>
#include <QWidget>

using namespace mu::notation;

namespace {
//! NOTE: Return/Enter or a click elsewhere (the editor losing focus) commits the typed value, Escape closes the editor
//! without changing anything. It's a window of its own that takes the keyboard: the score's shortcuts belong to the
//! main window (and are also kept off while typing, through ShortcutOverride)
class ValueEditorPopupFilter : public QObject
{
public:
    ValueEditorPopupFilter(QWidget* editor, QDoubleSpinBox* spinBox, std::function<void(double)> commit)
        : QObject(editor), m_editor(editor), m_spinBox(spinBox), m_commit(std::move(commit)) {}

    void markEdited() { m_edited = true; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::ShortcutOverride:
            event->accept();
            return true;
        case QEvent::KeyPress: {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                finish(true);
                return true;
            }
            if (key == Qt::Key_Escape) {
                finish(false);
                return true;
            }
            break;
        }
        case QEvent::WindowDeactivate:
            if (watched == m_editor) {
                finish(true);
            }
            break;
        default:
            break;
        }

        return QObject::eventFilter(watched, event);
    }

private:
    void finish(bool commit)
    {
        if (m_finished) {
            return;
        }
        m_finished = true;

        if (commit && m_edited) {
            m_spinBox->interpretText();
            m_commit(m_spinBox->value());
        }

        m_editor->close();
    }

    QWidget* m_editor = nullptr;
    QDoubleSpinBox* m_spinBox = nullptr;
    std::function<void(double)> m_commit;
    bool m_finished = false;
    bool m_edited = false;
};
}

static const QString VALUE_EDITOR_POPUP_FILTER_NAME = QStringLiteral("valueEditorPopupFilter");

//! NOTE: its filter (which commits when it loses focus) goes first
void mu::notation::closeValueEditorPopup(QPointer<QObject>& editor)
{
    if (!editor) {
        return;
    }

    delete editor->findChild<QObject*>(VALUE_EDITOR_POPUP_FILTER_NAME);
    if (QWidget* widget = qobject_cast<QWidget*>(editor.data())) {
        widget->close();
    }
    editor = nullptr;
}

void mu::notation::showValueEditorPopup(QPointer<QObject>& editor, const ValueEditorPopupParams& params, const QPointF& globalPos,
                                        std::function<void(double)> commit)
{
    closeValueEditorPopup(editor);

    QWidget* widget = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    widget->setAttribute(Qt::WA_DeleteOnClose);
    editor = widget;

    QHBoxLayout* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(2, 2, 2, 2);

    QDoubleSpinBox* spinBox = new QDoubleSpinBox(widget);
    spinBox->setDecimals(params.decimals);
    spinBox->setRange(params.min, params.max);
    spinBox->setValue(params.value);
    spinBox->setPrefix(params.prefix);
    spinBox->setSuffix(params.suffix);
    layout->addWidget(spinBox);

    const double min = params.min;
    const double max = params.max;
    ValueEditorPopupFilter* filter = new ValueEditorPopupFilter(widget, spinBox, [min, max, commit = std::move(commit)](double typed) {
        commit(std::clamp(typed, min, max));
    });
    filter->setObjectName(VALUE_EDITOR_POPUP_FILTER_NAME);
    // Typing, arrows or the wheel - connected after the initial setValue()
    QObject::connect(spinBox, &QDoubleSpinBox::textChanged, filter, [filter]() { filter->markEdited(); });
    widget->installEventFilter(filter);
    spinBox->installEventFilter(filter);

    widget->adjustSize();

    // Right of the click, or left of it near the screen's right edge, and always fully on screen
    const QPoint clickPos = globalPos.toPoint();
    QPoint pos = clickPos + QPoint(12, -widget->height() / 2);
    if (const QScreen* screen = QGuiApplication::screenAt(clickPos)) {
        const QRect available = screen->availableGeometry();
        if (pos.x() + widget->width() > available.right()) {
            pos.setX(clickPos.x() - 12 - widget->width());
        }
        pos.setX(std::clamp(pos.x(), available.left(), std::max(available.left(), available.right() - widget->width())));
        pos.setY(std::clamp(pos.y(), available.top(), std::max(available.top(), available.bottom() - widget->height())));
    }
    widget->move(pos);
    widget->show();
    widget->raise();
    widget->activateWindow();
    spinBox->setFocus(Qt::PopupFocusReason);
    spinBox->selectAll();
}
