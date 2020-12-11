/**
 * SPDX-FileCopyrightText: 2020 Tobias Fella <fella@posteo.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick 2.14
import QtQuick.Controls 2.14 as Controls
import QtQuick.Layouts 1.14

import org.kde.kirigami 2.12 as Kirigami

import org.kde.neochat 1.0

Kirigami.ScrollablePage {
    id: welcomePage
    
    title: i18n("Welcome")
    
    ColumnLayout {
        Kirigami.Icon {
            source: "neochat"
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 16
        }
        Controls.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 25
            text: i18n("Welcome to Matrix")     
        }
        Controls.Button {
            Layout.alignment: Qt.AlignHCenter
            text: i18n("Login")
            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            onClicked: pageStack.layers.push(homeserverPage, {'register': false})
        }
        Controls.Button {
            Layout.alignment: Qt.AlignHCenter
            text: i18n("Register")
            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            onClicked: pageStack.layers.push(homeserverPage, {'register': true})
        }
    }
    
    Component {
        id: homeserverPage
        HomeserverPage {
        }
    }    
}
