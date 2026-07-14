#include "RoomSession.h"

namespace te = tracktion;

static const char* const kServiceType = "motive-studio-room";

//==============================================================================
struct RoomSession::Peer : juce::InterprocessConnection
{
    explicit Peer (RoomSession& o)
        : InterprocessConnection (true, 0x4d6f7456), owner (o) {}

    ~Peer() override { disconnect(); }

    void connectionMade() override                                  { owner.handlePeerConnected (*this); }
    void connectionLost() override                                  { owner.handlePeerLost (*this); }
    void messageReceived (const juce::MemoryBlock& message) override { owner.handleMessage (*this, message); }

    RoomSession& owner;
};

struct RoomSession::Server : juce::InterprocessConnectionServer
{
    explicit Server (RoomSession& o) : owner (o) {}

    juce::InterprocessConnection* createConnectionObject() override
    {
        auto peer = new Peer (owner);

        const juce::ScopedLock sl (owner.peersLock);
        owner.peers.add (peer);
        return peer;
    }

    RoomSession& owner;
};

struct RoomSession::EditSync : juce::ValueTreeSynchroniser
{
    EditSync (juce::ValueTree tree, RoomSession& o)
        : juce::ValueTreeSynchroniser (tree), owner (o) {}

    void stateChanged (const void* encodedChange, size_t encodedChangeSize) override
    {
        if (! owner.applyingRemote)
            owner.broadcastDelta (encodedChange, encodedChangeSize);
    }

    RoomSession& owner;
};

//==============================================================================
RoomSession::RoomSession (te::Engine& e)
    : engine (e), serviceList (kServiceType, discoveryPort)
{
}

RoomSession::~RoomSession()
{
    leaveRoom();
}

juce::File RoomSession::getRoomsDirectory()
{
    auto dir = juce::File ("/Users/Shared/Motive Studio Rooms");
    dir.createDirectory();
    return dir;
}

//==============================================================================
bool RoomSession::hostRoom (const juce::String& name, const juce::File& roomEditFile)
{
    leaveRoom();

    roomName = name;
    roomDir = roomEditFile.getParentDirectory();

    server = std::make_unique<Server> (*this);

    if (! server->beginWaitingForSocket (connectionPort))
    {
        server = nullptr;
        return false;
    }

    advertiser = std::make_unique<juce::NetworkServiceDiscovery::Advertiser> (
        kServiceType, roomName, discoveryPort, connectionPort);

    mode = Mode::host;
    seedSharedFilesFromDisk();
    startTimer (2000);
    notifyStatus();
    return true;
}

bool RoomSession::joinRoom (const juce::String& hostAddress)
{
    leaveRoom();

    auto peer = std::make_unique<Peer> (*this);

    if (! peer->connectToSocket (hostAddress, connectionPort, 5000))
        return false;

    {
        const juce::ScopedLock sl (peersLock);
        peers.add (peer.release());
    }

    mode = Mode::client;
    startTimer (2000);
    notifyStatus();
    return true;
}

void RoomSession::leaveRoom()
{
    stopTimer();
    editSync = nullptr;
    advertiser = nullptr;

    if (server != nullptr)
        server->stop();

    {
        const juce::ScopedLock sl (peersLock);
        peers.clear();
    }

    server = nullptr;
    sharedFiles.clear();
    mode = Mode::none;
    roomName.clear();
    notifyStatus();
}

void RoomSession::attachToEdit (te::Edit* edit)
{
    attachedEdit = edit;
    editSync = nullptr;

    if (attachedEdit != nullptr && isInRoom())
        editSync = std::make_unique<EditSync> (attachedEdit->state, *this);
}

int RoomSession::getNumPeers() const
{
    const juce::ScopedLock sl (peersLock);

    int n = 0;
    for (auto* p : peers)
        if (p->isConnected())
            ++n;

    return n;
}

std::vector<juce::NetworkServiceDiscovery::Service> RoomSession::getAvailableRooms() const
{
    return serviceList.getServices();
}

//==============================================================================
void RoomSession::handlePeerConnected (Peer& peer)
{
    if (mode == Mode::host)
        sendFilesAndStateTo (peer);

    notifyStatus();
}

void RoomSession::handlePeerLost (Peer& peer)
{
    if (mode == Mode::client)
    {
        // host went away — the room is over, but the local copy of the project stays
        juce::MessageManager::callAsync ([this] { leaveRoom(); });
        return;
    }

    juce::MessageManager::callAsync ([this, peerPtr = &peer]
    {
        const juce::ScopedLock sl (peersLock);
        peers.removeObject (peerPtr);
        notifyStatus();
    });
}

void RoomSession::sendFilesAndStateTo (Peer& peer)
{
    if (attachedEdit == nullptr)
        return;

    juce::MemoryOutputStream hello;
    hello.writeByte (msgHello);
    hello.writeString (roomName);
    peer.sendMessage (hello.getMemoryBlock());

    for (auto& [relativePath, size] : sharedFiles)
    {
        juce::ignoreUnused (size);
        auto message = makeFileMessage (roomDir.getChildFile (relativePath));

        if (message.getSize() > 0)
            peer.sendMessage (message);
    }

    juce::MemoryOutputStream state;
    state.writeByte (msgFullState);
    state.writeString (attachedEdit->state.toXmlString());
    peer.sendMessage (state.getMemoryBlock());
}

//==============================================================================
void RoomSession::handleMessage (Peer& peer, const juce::MemoryBlock& message)
{
    if (message.getSize() < 1)
        return;

    juce::MemoryInputStream in (message, false);
    const auto type = in.readByte();

    if (type == msgHello)
    {
        roomName = in.readString();
        roomDir = getRoomsDirectory().getChildFile (juce::File::createLegalFileName (roomName));
        roomDir.createDirectory();
        notifyStatus();
    }
    else if (type == msgFile)
    {
        const auto relativePath = in.readString();
        const auto size = in.readInt64();

        juce::MemoryBlock data;
        in.readIntoMemoryBlock (data, (ssize_t) size);

        writeIncomingFile (relativePath, data);
        sharedFiles[relativePath] = (juce::int64) data.getSize();

        if (mode == Mode::host)
            sendToAll (message, &peer);
    }
    else if (type == msgFullState)
    {
        const auto xml = in.readString();
        auto editFile = roomDir.getChildFile (juce::File::createLegalFileName (roomName) + ".tracktionedit");
        editFile.replaceWithText (xml);

        if (onRemoteEditReady != nullptr)
            onRemoteEditReady (editFile);
    }
    else if (type == msgDelta)
    {
        applyRemoteDelta (message);

        if (mode == Mode::host)
            sendToAll (message, &peer);
    }
    else if (type == msgTransport)
    {
        const auto command = in.readInt();
        const auto position = in.readDouble();

        if (onRemoteTransport != nullptr)
        {
            const juce::ScopedValueSetter<bool> svs (applyingRemote, true);
            onRemoteTransport (command, position);
        }

        if (mode == Mode::host)
            sendToAll (message, &peer);
    }
}

void RoomSession::applyRemoteDelta (const juce::MemoryBlock& message)
{
    if (attachedEdit == nullptr)
        return;

    const juce::ScopedValueSetter<bool> svs (applyingRemote, true);

    auto root = attachedEdit->state;
    juce::ValueTreeSynchroniser::applyChange (root,
                                              juce::addBytesToPointer (message.getData(), 1),
                                              message.getSize() - 1, nullptr);
}

void RoomSession::broadcastDelta (const void* data, size_t size)
{
    juce::MemoryBlock message (size + 1);
    static_cast<char*> (message.getData())[0] = (char) msgDelta;
    message.copyFrom (data, 1, size);
    sendToAll (message);
}

void RoomSession::sendTransportCommand (int command, double positionSeconds)
{
    if (! isInRoom())
        return;

    juce::MemoryOutputStream out;
    out.writeByte (msgTransport);
    out.writeInt (command);
    out.writeDouble (positionSeconds);
    sendToAll (out.getMemoryBlock());
}

void RoomSession::sendToAll (const juce::MemoryBlock& message, juce::InterprocessConnection* except)
{
    const juce::ScopedLock sl (peersLock);

    for (auto* p : peers)
        if (p != except && p->isConnected())
            p->sendMessage (message);
}

//==============================================================================
bool RoomSession::isShareableAudioFile (const juce::File& f) const
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;m4a");
}

juce::MemoryBlock RoomSession::makeFileMessage (const juce::File& file) const
{
    juce::MemoryBlock data;

    if (! file.loadFileAsData (data))
        return {};

    juce::MemoryOutputStream out;
    out.writeByte (msgFile);
    out.writeString (file.getRelativePathFrom (roomDir).replaceCharacter ('\\', '/'));
    out.writeInt64 ((juce::int64) data.getSize());
    out.write (data.getData(), data.getSize());
    return out.getMemoryBlock();
}

void RoomSession::writeIncomingFile (const juce::String& relativePath, const juce::MemoryBlock& data)
{
    if (relativePath.contains (".."))
        return;

    auto target = roomDir;
    for (const auto& part : juce::StringArray::fromTokens (relativePath, "/", {}))
        if (part.isNotEmpty())
            target = target.getChildFile (juce::File::createLegalFileName (part));

    target.getParentDirectory().createDirectory();
    target.replaceWithData (data.getData(), data.getSize());

    // make sure any clip already pointing at this file picks up the real audio
    engine.getAudioFileManager().forceFileUpdate (te::AudioFile (engine, target));
}

void RoomSession::seedSharedFilesFromDisk()
{
    sharedFiles.clear();

    for (const auto& entry : juce::RangedDirectoryIterator (roomDir, true, "*", juce::File::findFiles))
    {
        auto f = entry.getFile();

        if (isShareableAudioFile (f))
            sharedFiles[f.getRelativePathFrom (roomDir).replaceCharacter ('\\', '/')] = f.getSize();
    }
}

void RoomSession::shareNewFiles()
{
    if (attachedEdit == nullptr || attachedEdit->getTransport().isRecording())
        return;

    for (const auto& entry : juce::RangedDirectoryIterator (roomDir, true, "*", juce::File::findFiles))
    {
        auto f = entry.getFile();

        if (! isShareableAudioFile (f))
            continue;

        // skip files that are still being written
        if (juce::Time::getCurrentTime() - f.getLastModificationTime() < juce::RelativeTime::seconds (1.5))
            continue;

        const auto key = f.getRelativePathFrom (roomDir).replaceCharacter ('\\', '/');
        const auto size = f.getSize();

        auto existing = sharedFiles.find (key);
        if (existing != sharedFiles.end() && existing->second == size)
            continue;

        sharedFiles[key] = size;

        auto message = makeFileMessage (f);
        if (message.getSize() > 0)
            sendToAll (message);
    }
}

void RoomSession::timerCallback()
{
    if (isInRoom())
        shareNewFiles();
}

void RoomSession::notifyStatus()
{
    if (onStatusChanged != nullptr)
        onStatusChanged();
}
