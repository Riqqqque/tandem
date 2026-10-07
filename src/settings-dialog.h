// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

class QWidget;

// Tabs: 0 = Streaming, 1 = Chat, 2 = Overlay. Saves and applies on OK.
void ShowTandemSettings(QWidget *parent, int tab = 0);
