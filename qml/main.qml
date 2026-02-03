import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WaylandCompositor

ApplicationWindow {
    id: window
    visible: true
    width: 1280
    height: 720
    title: "Wayland Compositor - " + compositor.socketName
    color: "#1e1e1e"
    
    /* Compositor instance is set from C++ */
    property var compositor: null
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10
        
        /* Header with socket info */
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "#2d2d2d"
            radius: 5
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 20
                
                Label {
                    text: "🖥️ Wayland Compositor"
                    font.pixelSize: 18
                    font.bold: true
                    color: "white"
                }
                
                Rectangle {
                    Layout.preferredWidth: 300
                    Layout.preferredHeight: 30
                    color: "#1a1a1a"
                    radius: 3
                    
                    Label {
                        anchors.centerIn: parent
                        text: compositor ? "WAYLAND_DISPLAY=" + compositor.socketName : "Not running"
                        font.family: "monospace"
                        font.pixelSize: 12
                        color: "#00ff00"
                    }
                }
                
                Label {
                    text: compositor ? compositor.viewCount + " view(s)" : "0 views"
                    color: "#aaa"
                }
                
                Item { Layout.fillWidth: true }
                
                Label {
                    text: "Click a view to focus, then type!"
                    color: "#888"
                    font.italic: true
                }
            }
        }
        
        /* Main content area with embedded views */
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            
            /* First embedded view */
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#2a2a2a"
                radius: 5
                border.color: embeddedView1.activeFocus ? "#4a9eff" : "#444"
                border.width: 2
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 5
                    
                    /* Title bar */
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        color: embeddedView1.activeFocus ? "#3a7abd" : "#3a3a3a"
                        radius: 3
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 5
                            
                            Label {
                                text: embeddedView1.hasView ? embeddedView1.title : "View 1 - Waiting for app..."
                                color: "white"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            
                            Button {
                                text: "✕"
                                visible: embeddedView1.hasView
                                Layout.preferredWidth: 25
                                Layout.preferredHeight: 25
                                onClicked: compositor.closeView(0)
                            }
                        }
                    }
                    
                    /* Embedded Wayland view */
                    EmbeddedView {
                        id: embeddedView1
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        viewIndex: 0
                        focus: true
                        
                        /* Placeholder when no view */
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            visible: !embeddedView1.hasView
                            
                            Label {
                                anchors.centerIn: parent
                                text: "Start an app with:\nWAYLAND_DISPLAY=" + (compositor ? compositor.socketName : "wayland-1") + " foot"
                                color: "#666"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 14
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            onClicked: embeddedView1.forceActiveFocus()
                            propagateComposedEvents: true
                        }
                    }
                }
            }
            
            /* Second embedded view */
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#2a2a2a"
                radius: 5
                border.color: embeddedView2.activeFocus ? "#4a9eff" : "#444"
                border.width: 2
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 5
                    
                    /* Title bar */
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        color: embeddedView2.activeFocus ? "#3a7abd" : "#3a3a3a"
                        radius: 3
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 5
                            
                            Label {
                                text: embeddedView2.hasView ? embeddedView2.title : "View 2 - Waiting for app..."
                                color: "white"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            
                            Button {
                                text: "✕"
                                visible: embeddedView2.hasView
                                Layout.preferredWidth: 25
                                Layout.preferredHeight: 25
                                onClicked: compositor.closeView(1)
                            }
                        }
                    }
                    
                    /* Embedded Wayland view */
                    EmbeddedView {
                        id: embeddedView2
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        viewIndex: 1
                        
                        /* Placeholder when no view */
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            visible: !embeddedView2.hasView
                            
                            Label {
                                anchors.centerIn: parent
                                text: "Second app will appear here"
                                color: "#666"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 14
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            onClicked: embeddedView2.forceActiveFocus()
                            propagateComposedEvents: true
                        }
                    }
                }
            }
        }
        
        /* Status bar */
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: "#2d2d2d"
            radius: 3
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: 5
                
                Label {
                    text: "Status: " + (compositor && compositor.running ? "Running" : "Stopped")
                    color: compositor && compositor.running ? "#0f0" : "#f00"
                }
                
                Item { Layout.fillWidth: true }
                
                Label {
                    text: "Focus: " + (embeddedView1.activeFocus ? "View 1" : (embeddedView2.activeFocus ? "View 2" : "None"))
                    color: "#aaa"
                }
            }
        }
    }
}
