#pragma once

#include <tracktion_engine/tracktion_engine.h>

// Real-time project collaboration. One user hosts a room; others join over the
// network. The whole Edit (clips, plugins, tempo, everything) syncs live via
// ValueTree deltas, audio files transfer automatically, and transport follows.
class RoomSession : private juce::Timer
{
public:
    explicit RoomSession (tracktion::Engine&);
    ~RoomSession() override;

    static constexpr int connectionPort = 47815;
    static constexpr int discoveryPort  = 47816;

    // identical absolute path on every Mac, so synced file references resolve everywhere
    static juce::File getRoomsDirectory();

    bool hostRoom (const juce::String& roomName, const juce::File& roomEditFile);
    bool joinRoom (const juce::String& hostAddress);
    void leaveRoom();

    void attachToEdit (tracktion::Edit*);

    enum TransportCommand { cmdStop = 0, cmdPlay = 1, cmdRecord = 2 };
    void sendTransportCommand (int command, double positionSeconds);

    std::vector<juce::NetworkServiceDiscovery::Service> getAvailableRooms() const;

    bool isInRoom() const noexcept                  { return mode != Mode::none; }
    bool isHost() const noexcept                    { return mode == Mode::host; }
    juce::String getRoomName() const noexcept       { return roomName; }
    int getNumPeers() const;
    bool isApplyingRemoteChange() const noexcept    { return applyingRemote; }

    std::function<void (juce::File)> onRemoteEditReady;
    std::function<void (int, double)> onRemoteTransport;
    std::function<void()> onStatusChanged;

private:
    enum class Mode { none, host, client };
    enum MessageType : char { msgHello = 1, msgFile = 2, msgFullState = 3, msgDelta = 4, msgTransport = 5 };

    struct Peer;
    struct Server;
    struct EditSync;

    tracktion::Engine& engine;
    tracktion::Edit* attachedEdit = nullptr;
    Mode mode = Mode::none;
    juce::String roomName;
    juce::File roomDir;
    bool applyingRemote = false;

    std::unique_ptr<Server> server;
    juce::CriticalSection peersLock;
    juce::OwnedArray<juce::InterprocessConnection> peers;
    std::unique_ptr<EditSync> editSync;
    std::unique_ptr<juce::NetworkServiceDiscovery::Advertiser> advertiser;
    juce::NetworkServiceDiscovery::AvailableServiceList serviceList;
    std::map<juce::String, juce::int64> sharedFiles;   // room-relative path -> size

    void broadcastDelta (const void*, size_t);
    void sendToAll (const juce::MemoryBlock&, juce::InterprocessConnection* except = nullptr);
    void handleMessage (Peer&, const juce::MemoryBlock&);
    void handlePeerConnected (Peer&);
    void handlePeerLost (Peer&);
    void sendFilesAndStateTo (Peer&);
    void seedSharedFilesFromDisk();
    void shareNewFiles();
    void applyRemoteDelta (const juce::MemoryBlock&);
    void writeIncomingFile (const juce::String& relativePath, const juce::MemoryBlock& data);
    juce::MemoryBlock makeFileMessage (const juce::File&) const;
    bool isShareableAudioFile (const juce::File&) const;
    void timerCallback() override;
    void notifyStatus();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomSession)
};
