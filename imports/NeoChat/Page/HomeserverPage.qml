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
    id: homeserverPage
    
    title: i18n("Homeserver")
    
    property var homeserver: customHomeserver.visible ? customHomeserver.text : serverCombo.currentText
    property bool register: false
    
    Component.onCompleted: Controller.testConnection(homeserver)
    
    onHomeserverChanged: {
        continueButton.enabled = false
        Controller.testConnection(homeserver)
    }
    
    Connections {
        target: Controller
        onTestConnectionResult: {
            if(homeserver === connection) {
                continueButton.enabled = usable
            }
        }
    }
    
    ColumnLayout {
        Kirigami.Icon {
            source: "neochat"
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 16
        }
        Controls.Label {
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 18
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            Layout.alignment: Qt.AlignHCenter
            wrapMode: Text.WordWrap
            text: register ? i18n("Choose a homeserver. It's not important which server you choose, you can chat with people on any server") : i18n("Select your homeserver")     
        }
        Kirigami.FormLayout {
            Controls.ComboBox {
                id: serverCombo
                
                Kirigami.FormData.label: i18n("Homeserver:")
                model: ["matrix.org", "kde.org", "tchncs.de", i18n("Other...")]
                Layout.alignment: Qt.AlignHCenter
            }
            Controls.TextField {
                id: customHomeserver
                
                Kirigami.FormData.label: i18n("Url:")
                visible: serverCombo.currentIndex === 3
                onTextChanged: {
                    Controller.testConnection(text)
                }
            }
            Controls.Button {
                id: continueButton
                text: i18n("Continue")
                enabled: false
                onClicked: {

                }
            }
        }
    }
}
