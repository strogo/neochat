import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Controls.Material 2.2

import Spectral.Setting 0.1
import Spectral.Font 0.1

Text {
    property alias icon: materialLabel.text

    id: materialLabel

    color: Material.foreground
    font.pointSize: 16
    font.family: MaterialFont.name
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
}
