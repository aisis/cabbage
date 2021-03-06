/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2013 - Raw Material Software Ltd.

   Permission is granted to use this software under the terms of either:
   a) the GPL v2 (or any later version)
   b) the Affero GPL v3

   Details of these licenses can be found at: www.gnu.org/licenses

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

   ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.juce.com for more information.

  ==============================================================================
*/

#include "../JuceLibraryCode/JuceHeader.h"
#include "MainHostWindow.h"
#include "FilterGraph.h"
#include "InternalFilters.h"
#include "GraphEditorPanel.h"
#include "PluginWrapperProcessor.h"
#include "../Plugin/CabbagePluginProcessor.h"
#include "AudioFilePlaybackProcessor.h"
#include "AutomationProcessor.h"


//==============================================================================
const int FilterGraph::midiChannelNumber = 0x1000;

FilterGraph::FilterGraph (AudioPluginFormatManager& formatManager_)
    : FileBasedDocument (filenameSuffix,
                         filenameWildcard,
                         "Load a filter graph",
                         "Save a filter graph"),
    formatManager (formatManager_),
    lastUID (0),
    audioPlayHead(),
    timeInSeconds(0),
    currentBPM(60),
    playPosition(0),
    PPQN(24),
    ppqPosition(1),
    automationNodeID(-1),
    subTicks(0)
{
    startTimer(0);
    stopTimer();
    setChangedFlag (false);
    setBPM(60);
}

FilterGraph::~FilterGraph()
{
    stopTimer();
    graph.clear();
}

uint32 FilterGraph::getNextUID() noexcept
{
    return ++lastUID;
}
//==============================================================================
int FilterGraph::getNumFilters() const noexcept
{
    return graph.getNumNodes();
}

const AudioProcessorGraph::Node::Ptr FilterGraph::getNode (const int index) const noexcept
{
    return graph.getNode (index);
}

const AudioProcessorGraph::Node::Ptr FilterGraph::getNodeForId (const uint32 uid) const noexcept
{
    return graph.getNodeForId (uid);
}

//==============================================================================
void FilterGraph::addNodesToAutomationTrack(int32 id, int index)
{
    /*
        //if there is an automation device, otherwise create one. Only one permitted in each patch..
        if(getNodeForId(automationNodeID))
        {
            automationAdded=true;
            AutomationProcessor* node = (AutomationProcessor*)graph.getNodeForId(automationNodeID)->getProcessor();
            node->addAutomatableNode(graph.getNodeForId(id)->getProcessor()->getName(), graph.getNodeForId(id)->getProcessor()->getParameterName(index), id, index);
        }
        else
        {
            PluginDescription descript;
            descript.descriptiveName = "Automation track";
            descript.name = "AutomationTrack";
            descript.pluginFormatName = "AutomationTrack";
            descript.numInputChannels = 2;
            descript.numOutputChannels = 2;
            addFilter(&descript, 0.50f, 0.5f);

            AutomationProcessor* node = (AutomationProcessor*)graph.getNodeForId(automationNodeID)->getProcessor();
            node->addAutomatableNode(graph.getNodeForId(id)->getProcessor()->getName(), graph.getNodeForId(id)->getProcessor()->getParameterName(index), id, index);

        }
    */
}

//==============================================================================
// transport methods
//==============================================================================
void FilterGraph::setIsPlaying(bool value, bool reset)
{
    audioPlayHead.setIsPlaying(value);
    if(value == true)
    {
        startTimer(100*(60.f/currentBPM));
    }
    else
        stopTimer();

    if(reset==true)
    {
        timeInSeconds=0;
        audioPlayHead.setPPQPosition(0);
        audioPlayHead.setTimeInSeconds(0);
        ppqPosition=0;
        timeInSeconds=0;

    }
}
//------------------------------------------
void FilterGraph::setBPM(int bpm)
{
    currentBPM = bpm;
    audioPlayHead.setBpm(bpm);


    if(isTimerRunning())
    {
        stopTimer();
        startTimer(100*(60.f/currentBPM));
    }
}
//------------------------------------------
void FilterGraph::hiResTimerCallback()
{
    if(playPosition==0)
    {
        timeInSeconds++;
        audioPlayHead.setTimeInSeconds(timeInSeconds);
    }


    if(subTicks==0)
    {
        audioPlayHead.setPPQPosition(ppqPosition);
        ppqPosition++;
    }

    subTicks = (subTicks > 9 ? 0 : subTicks+1);
    playPosition = (playPosition > 1 ? 0 : playPosition+((float)getTimerInterval()/1000.f));
}

AudioProcessorGraph::Node::Ptr FilterGraph::createNode(const PluginDescription* desc, int uid)
{
    AudioProcessorGraph::Node* node = nullptr;
    String errorMessage;


    if(desc->pluginFormatName=="AutomationTrack")
    {
        if (AutomationProcessor* automation = new AutomationProcessor(this))
        {
            automation->setPlayConfigDetails(2,
                                             2,
                                             graph.getSampleRate(),
                                             graph.getBlockSize());

            if(uid!=-1)
                node = graph.addNode (automation, uid);
            else
                node = graph.addNode (automation);

            automationNodeID = node->nodeId;
            node->properties.set("pluginType", "AutomationTrack");
            node->properties.set("pluginName", "AutomationTrack");
            ScopedPointer<XmlElement> xmlElem;
            xmlElem = desc->createXml();
            String xmlText = xmlElem->createDocument("");
            node->properties.set("pluginType", "AutomationTrack");
            node->properties.set("pluginDesc", xmlText);
            node->getProcessor()->setPlayHead(&audioPlayHead);
            return node;
        }
    }

    if(desc->pluginFormatName=="SoundfilePlayer")
    {
        if (AudioFilePlaybackProcessor* soundfiler = new AudioFilePlaybackProcessor())
        {
            soundfiler->setPlayConfigDetails(2,
                                             2,
                                             graph.getSampleRate(),
                                             graph.getBlockSize());

            soundfiler->setupAudioFile(File(desc->fileOrIdentifier));

            if(uid!=-1)
                node = graph.addNode (soundfiler, uid);
            else
                node = graph.addNode (soundfiler);

            node->properties.set("pluginType", "SoundfilePlayer");
            node->properties.set("pluginName", "Soundfile Player");
            ScopedPointer<XmlElement> xmlElem;
            xmlElem = desc->createXml();
            String xmlText = xmlElem->createDocument("");
            node->properties.set("pluginDesc", xmlText);
            node->getProcessor()->setPlayHead(&audioPlayHead);
            return node;
        }
    }
    else if(desc->pluginFormatName=="Internal")
    {
        if (AudioPluginInstance* instance = formatManager.createPluginInstance (*desc, graph.getSampleRate(), graph.getBlockSize(), errorMessage))
        {
            if(uid!=-1)
                node = graph.addNode (instance, uid);
            else
                node = graph.addNode (instance);

            node->properties.set("pluginType", "Internal");
            node->properties.set("pluginName", desc->name);
        }
    }

    else if(desc->pluginFormatName=="Cabbage")
    {
        CabbagePluginAudioProcessor* cabbageNativePlugin = new CabbagePluginAudioProcessor(desc->fileOrIdentifier, false, AUDIO_PLUGIN);
        int numChannels = cUtils::getNchnlsFromFile(File(desc->fileOrIdentifier).loadFileAsString());
        //create GUI for selected plugin...
        cabbageNativePlugin->initialiseWidgets(File(desc->fileOrIdentifier).loadFileAsString(), true);
        cabbageNativePlugin->addWidgetsToEditor(true);
        cabbageNativePlugin->setPlayConfigDetails(numChannels,
                numChannels,
                cabbageNativePlugin->getCsoundSamplingRate(),
                cabbageNativePlugin->getCsoundKsmpsSize());

        if(uid!=-1)
            node = graph.addNode (cabbageNativePlugin, uid);
        else
            node = graph.addNode (cabbageNativePlugin);

        node->properties.set("pluginName", cabbageNativePlugin->getPluginName());
        //native Cabbage plugins don't have plugin descriptors, so we create one here..
        ScopedPointer<XmlElement> xmlElem;
        xmlElem = desc->createXml();
        String xmlText = xmlElem->createDocument("");
        node->properties.set("pluginType", "Cabbage");
        node->properties.set("pluginDesc", xmlText);
        node->getProcessor()->setPlayHead(&audioPlayHead);
    }

    else //all third party plugins get wrapped into a PluginWrapper...
    {
        if(PluginWrapper* instance = new PluginWrapper(formatManager.createPluginInstance (*desc, graph.getSampleRate(), graph.getBlockSize(), errorMessage)))
        {
            instance->setPlayConfigDetails( desc->numInputChannels,
                                            desc->numOutputChannels,
                                            graph.getSampleRate(),
                                            graph.getBlockSize());
            instance->setPluginName(desc->name);
            //cUtils::debug("num params", instance->getNumParameters());
            if(uid!=-1)
                node = graph.addNode (instance, uid);
            else
                node = graph.addNode (instance);

            node->properties.set("pluginType", "ThirdParty");
            node->getProcessor()->setPlayHead(&audioPlayHead);
            node->properties.set("pluginName", desc->name);
        }
    }

    return node;
}

//==============================================================================
void FilterGraph::addFilter (const PluginDescription* desc, double x, double y)
{
    if (desc != nullptr)
    {
        AudioProcessorGraph::Node* node = nullptr;

        node = createNode(desc);

        if (node != nullptr)
        {
            node->properties.set ("x", x);
            node->properties.set ("y", y);
            lastNodeID = node->nodeId;
            //create node listener with unique nodeID;
            audioProcessorListeners.add(new NodeAudioProcessorListener(node->nodeId));
            audioProcessorListeners[audioProcessorListeners.size()-1]->addChangeListener(this);
            node->getProcessor()->addListener(audioProcessorListeners[audioProcessorListeners.size()-1]);
            changed();
        }
        else
        {
            AlertWindow::showMessageBox (AlertWindow::WarningIcon,
                                         TRANS("Couldn't create filter"),
                                         "Error loading plugin");
        }
    }
}

//==============================================================================
String FilterGraph::findControllerForparameter(int32 nodeID, int paramIndex)
{
    for(int i=0; i<midiMappings.size(); i++)
    {
        //cUtils::debug(midiMappings.getReference(i).nodeId);
        //cUtils::debug(midiMappings.getReference(i).parameterIndex);

        if(midiMappings.getReference(i).nodeId==nodeID && midiMappings.getReference(i).parameterIndex==paramIndex)
        {
            String midiInfo = "CC:"+String(midiMappings.getReference(i).controller)+" Chan:"+String(midiMappings.getReference(i).channel);
            return midiInfo;
        }
    }
    return String::empty;
}


//==============================================================================
void FilterGraph::removeFilter (const uint32 id)
{
    PluginWindow::closeCurrentlyOpenWindowsFor (id);

    if (graph.removeNode (id))
        changed();
}

void FilterGraph::disconnectFilter (const uint32 id)
{
    if (graph.disconnectNode (id))
        changed();
}

void FilterGraph::removeIllegalConnections()
{
    if (graph.removeIllegalConnections())
        changed();
}

void FilterGraph::setNodePosition (const int nodeId, double x, double y)
{
    const AudioProcessorGraph::Node::Ptr n (graph.getNodeForId (nodeId));

    if (n != nullptr)
    {
        n->properties.set ("x", jlimit (0.0, 1.0, x));
        n->properties.set ("y", jlimit (0.0, 1.0, y));
    }
}

void FilterGraph::getNodePosition (const int nodeId, double& x, double& y) const
{
    x = y = 0;

    const AudioProcessorGraph::Node::Ptr n (graph.getNodeForId (nodeId));

    if (n != nullptr)
    {
        x = (double) n->properties ["x"];
        y = (double) n->properties ["y"];
    }
}

int FilterGraph::getNumConnections() const noexcept
{
    return graph.getNumConnections();
}

const AudioProcessorGraph::Connection* FilterGraph::getConnection (const int index) const noexcept
{
    return graph.getConnection (index);
}

const AudioProcessorGraph::Connection* FilterGraph::getConnectionBetween (uint32 sourceFilterUID, int sourceFilterChannel,
        uint32 destFilterUID, int destFilterChannel) const noexcept
{
    return graph.getConnectionBetween (sourceFilterUID, sourceFilterChannel,
                                       destFilterUID, destFilterChannel);
}

bool FilterGraph::canConnect (uint32 sourceFilterUID, int sourceFilterChannel,
                              uint32 destFilterUID, int destFilterChannel) const noexcept
{
    return graph.canConnect (sourceFilterUID, sourceFilterChannel,
                             destFilterUID, destFilterChannel);
}

bool FilterGraph::addConnection (uint32 sourceFilterUID, int sourceFilterChannel,
                                 uint32 destFilterUID, int destFilterChannel)
{
    const bool result = graph.addConnection (sourceFilterUID, sourceFilterChannel,
                        destFilterUID, destFilterChannel);

    if (result)
        changed();

    return result;
}

void FilterGraph::removeConnection (const int index)
{
    graph.removeConnection (index);
    changed();
}

void FilterGraph::removeConnection (uint32 sourceFilterUID, int sourceFilterChannel,
                                    uint32 destFilterUID, int destFilterChannel)
{
    if (graph.removeConnection (sourceFilterUID, sourceFilterChannel,
                                destFilterUID, destFilterChannel))
        changed();
}

void FilterGraph::clear()
{
    PluginWindow::closeAllCurrentlyOpenWindows();

    graph.clear();
    changed();
}

//==============================================================================
String FilterGraph::getDocumentTitle()
{
    if (! getFile().exists())
        return "Unnamed";

    return getFile().getFileNameWithoutExtension();
}

Result FilterGraph::loadDocument (const File& file)
{
    graph.clear();
    XmlDocument doc (file);
    ScopedPointer<XmlElement> xml (doc.getDocumentElement());

    if (xml == nullptr || ! xml->hasTagName ("FILTERGRAPH"))
        return Result::fail ("Not a valid filter graph file");

    restoreFromXml (*xml);
    return Result::ok();
}

Result FilterGraph::saveDocument (const File& file)
{
    ScopedPointer<XmlElement> xml (createXml());

    if (! xml->writeToFile (file, String::empty))
        return Result::fail ("Couldn't write to the file");

    return Result::ok();
}

File FilterGraph::getLastDocumentOpened()
{
    RecentlyOpenedFilesList recentFiles;
    recentFiles.restoreFromString (getAppProperties().getUserSettings()
                                   ->getValue ("recentFilterGraphFiles"));

    return recentFiles.getFile (0);
}

void FilterGraph::setLastDocumentOpened (const File& file)
{
    RecentlyOpenedFilesList recentFiles;
    recentFiles.restoreFromString (getAppProperties().getUserSettings()
                                   ->getValue ("recentFilterGraphFiles"));

    recentFiles.addFile (file);

    getAppProperties().getUserSettings()
    ->setValue ("recentFilterGraphFiles", recentFiles.toString());
}

//==============================================================================
static XmlElement* createNodeXml (AudioProcessorGraph::Node* const node) noexcept
{
    PluginDescription pd;

    if(AudioPluginInstance* plugin = dynamic_cast <AudioPluginInstance*> (node->getProcessor()))
        plugin->fillInPluginDescription (pd);
    else if(PluginWrapper* plugin = dynamic_cast <PluginWrapper*> (node->getProcessor()))
        plugin->fillInPluginDescription (pd);
    else if(dynamic_cast <CabbagePluginAudioProcessor*> (node->getProcessor())||
    dynamic_cast <AudioFilePlaybackProcessor*> (node->getProcessor())||
    dynamic_cast <AutomationProcessor*> (node->getProcessor()))
    {
        //grab description of native plugin for saving...
        String xmlPluginDescriptor = node->properties.getWithDefault("pluginDesc", "").toString();
        //cUtils::debug(xmlPluginDescriptor);
        XmlElement* xmlElem;
        xmlElem = XmlDocument::parse(xmlPluginDescriptor);
        pd.loadFromXml(*xmlElem);
    }


    XmlElement* e = new XmlElement ("FILTER");
    e->setAttribute ("uid", (int) node->nodeId);
    e->setAttribute ("x", node->properties ["x"].toString());
    e->setAttribute ("y", node->properties ["y"].toString());
    e->setAttribute ("uiLastX", node->properties ["uiLastX"].toString());
    e->setAttribute ("uiLastY", node->properties ["uiLastY"].toString());
    e->addChildElement (pd.createXml());

    XmlElement* state = new XmlElement ("STATE");

    MemoryBlock m;
    node->getProcessor()->getStateInformation (m);
    state->addTextElement (m.toBase64Encoding());
    e->addChildElement (state);

    return e;
}

//==============================================================================
void FilterGraph::createNodeFromXml (const XmlElement& xml)
{
    PluginDescription desc;

    forEachXmlChildElement (xml, e)
    {
        if (desc.loadFromXml (*e))
            break;
    }

    AudioProcessorGraph::Node::Ptr node = nullptr;

    String errorMessage;
    node = createNode(&desc, xml.getIntAttribute ("uid"));

    if (const XmlElement* const state = xml.getChildByName ("STATE"))
    {
        MemoryBlock m;
        m.fromBase64Encoding (state->getAllSubText());

        node->getProcessor()->setStateInformation (m.getData(), (int) m.getSize());
    }

    node->properties.set ("x", xml.getDoubleAttribute ("x"));
    node->properties.set ("y", xml.getDoubleAttribute ("y"));
    node->properties.set ("uiLastX", xml.getIntAttribute ("uiLastX"));
    node->properties.set ("uiLastY", xml.getIntAttribute ("uiLastY"));
    node->properties.set("pluginName", desc.name);

}

//==============================================================================
XmlElement* FilterGraph::createXml() const
{
    XmlElement* xml = new XmlElement ("FILTERGRAPH");
    //cUtils::debug("graph.getNumNodes()", graph.getNumNodes());

    for (int i = 0; i < graph.getNumNodes(); ++i)
        xml->addChildElement (createNodeXml (graph.getNode (i)));

    for (int i = 0; i < graph.getNumConnections(); ++i)
    {
        const AudioProcessorGraph::Connection* const fc = graph.getConnection(i);

        XmlElement* e = new XmlElement ("CONNECTION");

        e->setAttribute ("srcFilter", (int) fc->sourceNodeId);
        e->setAttribute ("srcChannel", fc->sourceChannelIndex);
        e->setAttribute ("dstFilter", (int) fc->destNodeId);
        e->setAttribute ("dstChannel", fc->destChannelIndex);

        xml->addChildElement (e);
    }

    for (int i = 0; i < midiMappings.size(); ++i)
    {
        //add midi mapping to here, might not be easy...
        XmlElement* e = new XmlElement ("MIDI_MAPPINGS");
        e->setAttribute ("NodeId", midiMappings.getReference(i).nodeId);
        e->setAttribute ("ParameterIndex", midiMappings.getReference(i).parameterIndex);
        e->setAttribute ("Channel", midiMappings.getReference(i).channel);
        e->setAttribute ("Controller", midiMappings.getReference(i).controller);

        xml->addChildElement (e);
    }

    return xml;
}

void FilterGraph::restoreFromXml (const XmlElement& xml)
{
    clear();

    forEachXmlChildElementWithTagName (xml, e, "FILTER")
    {
        createNodeFromXml (*e);
        changed();
    }

    forEachXmlChildElementWithTagName (xml, e, "CONNECTION")
    {
        //cUtils::debug("srcFilter", e->getIntAttribute ("srcFilter"));
        addConnection ((uint32) e->getIntAttribute ("srcFilter"),
                       e->getIntAttribute ("srcChannel"),
                       (uint32) e->getIntAttribute ("dstFilter"),
                       e->getIntAttribute ("dstChannel"));
    }
    graph.removeIllegalConnections();

    forEachXmlChildElementWithTagName (xml, e, "MIDI_MAPPINGS")
    {
        midiMappings.add(CabbageMidiMapping(e->getIntAttribute ("NodeId"),
                                            e->getIntAttribute ("ParameterIndex"),
                                            e->getIntAttribute ("Channel"),
                                            e->getIntAttribute ("Controller")));
    }
}

void FilterGraph::updateAutomatedNodes(int nodeId, int parameterIndex, float value)
{
    graph.getNodeForId(nodeId)->getProcessor()->setParameterNotifyingHost(parameterIndex, value);
}


void FilterGraph::changeListenerCallback(ChangeBroadcaster* source)
{
    if(NodeAudioProcessorListener* listener = dynamic_cast<NodeAudioProcessorListener*>(source))
    {
        lastChangedNodeId = listener->nodeId;
        lastChangedNodeParameter = listener->parameterIndex;
    }

    else if(CabbagePropertiesPanel* props = dynamic_cast<CabbagePropertiesPanel*>(source))
    {
        CabbagePluginAudioProcessor* processor = dynamic_cast<CabbagePluginAudioProcessor*>(getNodeForId(this->getEditedNodeId())->getProcessor());
        if(processor)
        {
            CabbagePluginAudioProcessorEditor* editor = (CabbagePluginAudioProcessorEditor*)processor->getActiveEditor();
            if(editor)
            {
                editor->propsWindow->updatePropertyPanel(props);
                editor->propsWindow->updateIdentifiers();
            }
        }
    }
}

void FilterGraph::actionListenerCallback (const String &message)
{
    sendActionMessage(message);
}
//==========================================================================
// parameter callback for node, used to map midi messages to parameters
//==========================================================================
void NodeAudioProcessorListener::audioProcessorParameterChanged(AudioProcessor* processor, int index, float newValue)
{
    parameterIndex = index;
    sendChangeMessage();
}

