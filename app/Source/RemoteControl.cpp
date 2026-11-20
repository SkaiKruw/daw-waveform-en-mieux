#include "RemoteControl.h"
#include "BinaryData.h"

namespace
{
    juce::String jsonString (const juce::String& s) { return juce::JSON::toString (juce::var (s)); }

    const char* reasonFor (int status)
    {
        switch (status)
        {
            case 200: return "OK";
            case 400: return "Bad Request";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 429: return "Too Many Requests";
            default:  return "Error";
        }
    }

    // Adresses privées et Tailscale (100.64.0.0/10) : seules proposées au téléphone.
    juce::String labelFor (const juce::IPAddress& a)
    {
        const auto b = a.address;
        if (b[0] == 100 && b[1] >= 64 && b[1] <= 127) return U ("Tailscale (à distance)");
        if (b[0] == 192 && b[1] == 168) return U ("Réseau local");
        if (b[0] == 10) return U ("Réseau local");
        if (b[0] == 172 && b[1] >= 16 && b[1] <= 31) return U ("Réseau local");
        return {};
    }
}

RemoteControl::RemoteControl (EditContext& c) : juce::Thread ("Onde telecommande"), ctx (c) {}

RemoteControl::~RemoteControl() { stop(); }

bool RemoteControl::start (int p, bool localOnly)
{
    stop();
    port = p;
    listener = std::make_unique<juce::StreamingSocket>();
    if (! listener->createListener (port, localOnly ? "127.0.0.1" : juce::String()))
    {
        listener.reset();
        return false;
    }
    code = juce::String (juce::Random::getSystemRandom().nextInt ({ 100000, 1000000 }));
    badAttempts = 0;
    lockedUntil = 0;
    timerCallback();
    startTimerHz (10);
    startThread();
    return true;
}

void RemoteControl::stop()
{
    stopTimer();
    signalThreadShouldExit();
    if (listener) listener->close();     // débloque waitForNextConnection()
    stopThread (3000);
    listener.reset();
}

juce::Array<RemoteControl::Address> RemoteControl::getAddresses() const
{
    juce::Array<Address> r;
    for (auto& a : juce::IPAddress::getAllAddresses (false))
    {
        const auto label = labelFor (a);
        if (label.isEmpty()) continue;
        r.add ({ "http://" + a.toString() + ":" + juce::String (port) + "/?k=" + code, label + " : " + a.toString() });
    }
    // Réseau local d'abord (le cas courant), Tailscale ensuite.
    std::stable_sort (r.begin(), r.end(), [] (const Address& x, const Address& y) { return x.label.startsWith (U ("Réseau")) && ! y.label.startsWith (U ("Réseau")); });
    return r;
}

bool RemoteControl::isAllowedAction (const juce::String& id)
{
    static const juce::StringArray blocked { "file.new", "file.open", "file.saveas", "file.import", "file.export", "file.quit", "file.audio",
                                             "audio.import", "plugins.scan", "plugins.crash", "palette.open", "help.about", "help.shortcuts",
                                             "song.tempo", "audio.separate", "studio.remote" };
    return ! blocked.contains (id) && ! id.startsWith ("plugin.add.");
}

//==============================================================================
void RemoteControl::timerCallback()
{
    const auto state = buildState(), actions = buildActions();
    bool changed = false;
    {
        const juce::ScopedLock sl (stateLock);
        changed = state != stateJson;
        stateJson = state;
        actionsJson = actions;
    }
    if (changed && onStateChanged) onStateChanged();
}

juce::String RemoteControl::buildState() const
{
    auto* edit = ctx.edit;
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty ("project", edit != nullptr ? juce::var (edit->getName()) : juce::var (""));
    if (edit != nullptr)
    {
        auto& tr = edit->getTransport();
        const double t = tr.getPosition().inSeconds();
        o->setProperty ("playing", tr.isPlaying());
        o->setProperty ("recording", tr.isRecording());
        o->setProperty ("looping", (bool) tr.looping.get());
        o->setProperty ("click", (bool) edit->clickTrackEnabled.get());
        o->setProperty ("snap", ctx.snapOn);
        o->setProperty ("seconds", t);
        o->setProperty ("bars", ctx.formatBars (t, false).trim());
        o->setProperty ("time", EditContext::formatTime (t));
        o->setProperty ("bpm", ctx.bpmAt (t));
        o->setProperty ("sig", juce::String (ctx.beatsPerBar()) + "/4");
        o->setProperty ("canUndo", edit->getUndoManager().canUndo());
        juce::Array<juce::var> tracks;
        auto* sel = ctx.getSelectedTrack();
        int i = 0;
        for (auto* at : te::getAudioTracks (*edit))
        {
            juce::DynamicObject::Ptr t = new juce::DynamicObject();
            t->setProperty ("i", i++);
            t->setProperty ("name", at->getName());
            t->setProperty ("color", at->getColour().toDisplayString (false));
            t->setProperty ("mute", at->isMuted (false));
            t->setProperty ("solo", at->isSolo (false));
            t->setProperty ("arm", ctx.isTrackArmed (*at));
            t->setProperty ("sel", at == sel);
            tracks.add (juce::var (t.get()));
        }
        o->setProperty ("tracks", tracks);
    }
    return juce::JSON::toString (juce::var (o.get()), true);
}

juce::String RemoteControl::buildActions() const
{
    juce::Array<juce::var> list;
    for (auto* a : ctx.actions)
    {
        if (! isAllowedAction (a->id) || a->id.endsWithChar ('.')) continue;
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("id", a->id);
        o->setProperty ("name", a->name.trimCharactersAtEnd (U ("…")));
        o->setProperty ("cat", a->category);
        o->setProperty ("on", a->isTicked ? juce::var (a->isTicked()) : juce::var());
        list.add (juce::var (o.get()));
    }
    return juce::JSON::toString (juce::var (list), true);
}

//==============================================================================
void RemoteControl::run()
{
    while (! threadShouldExit() && listener != nullptr)
    {
        std::unique_ptr<juce::StreamingSocket> client (listener->waitForNextConnection());
        if (client == nullptr || threadShouldExit()) continue;
        serve (*client);
    }
}

void RemoteControl::serve (juce::StreamingSocket& s)
{
    // Requête HTTP/1.1 minimale : ligne de requête + en-têtes (le corps est ignoré, tout passe par la requête).
    juce::MemoryBlock buf;
    char tmp[2048];
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;
    while (buf.getSize() < 16384 && juce::Time::getMillisecondCounter() < deadline)
    {
        if (s.waitUntilReady (true, 200) <= 0) continue;
        const int n = s.read (tmp, (int) sizeof (tmp), false);
        if (n <= 0) break;
        buf.append (tmp, (size_t) n);
        if (buf.toString().contains ("\r\n\r\n")) break;
    }
    const auto request = buf.toString();
    const auto line = request.upToFirstOccurrenceOf ("\r\n", false, false);
    const auto method = line.upToFirstOccurrenceOf (" ", false, false);
    const auto target = line.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);

    int status = 400;
    juce::String type = "text/plain; charset=utf-8", body = "requete invalide";
    if (method.isNotEmpty() && target.startsWithChar ('/'))
        body = handle (method, target, status, type);

    const auto bodyUtf8 = body.toStdString();
    juce::String header;
    header << "HTTP/1.1 " << status << " " << reasonFor (status) << "\r\n"
           << "Content-Type: " << type << "\r\n"
           << "Content-Length: " << (int) bodyUtf8.size() << "\r\n"
           << "Cache-Control: no-store\r\n"
           << "Connection: close\r\n\r\n";
    const auto h = header.toStdString();
    s.write (h.data(), (int) h.size());
    if (! bodyUtf8.empty()) s.write (bodyUtf8.data(), (int) bodyUtf8.size());
}

juce::StringPairArray RemoteControl::parseQuery (const juce::String& q)
{
    juce::StringPairArray r;
    for (auto& part : juce::StringArray::fromTokens (q, "&", {}))
    {
        const auto k = juce::URL::removeEscapeChars (part.upToFirstOccurrenceOf ("=", false, false));
        const auto v = juce::URL::removeEscapeChars (part.fromFirstOccurrenceOf ("=", false, false).replaceCharacter ('+', ' '));
        if (k.isNotEmpty()) r.set (k, v);
    }
    return r;
}

bool RemoteControl::checkCode (const juce::StringPairArray& query, int& status)
{
    const auto now = juce::Time::getMillisecondCounter();
    if (now < lockedUntil) { status = 429; return false; }
    if (query["k"] == code && code.isNotEmpty()) { badAttempts = 0; lastClientMs = now; return true; }
    if (++badAttempts >= 10) { lockedUntil = now + 30000; badAttempts = 0; }
    status = 403;
    return false;
}

juce::String RemoteControl::handleForTest (const juce::String& method, const juce::String& target, int& status)
{
    juce::String type;
    return handle (method, target, status, type);
}

juce::String RemoteControl::handle (const juce::String& method, const juce::String& target, int& status, juce::String& type)
{
    const auto path = target.upToFirstOccurrenceOf ("?", false, false);
    const auto query = parseQuery (target.fromFirstOccurrenceOf ("?", false, false));
    type = "application/json; charset=utf-8";

    if (path == "/" || path == "/index.html")
    {
        status = 200;
        type = "text/html; charset=utf-8";
        return juce::String::fromUTF8 (BinaryData::remote_html, BinaryData::remote_htmlSize);
    }
    if (path == "/manifest.json")
    {
        status = 200;
        return R"({"name":"Onde Télécommande","short_name":"Onde","display":"standalone","background_color":"#16181c","theme_color":"#16181c","start_url":"/"})";
    }
    if (! path.startsWith ("/api/")) { status = 404; return R"({"error":"introuvable"})"; }
    if (! checkCode (query, status)) return status == 429 ? R"({"error":"trop d'essais, attendez 30 s"})" : R"({"error":"code incorrect"})";

    if (path == "/api/state")   { status = 200; const juce::ScopedLock sl (stateLock); return stateJson; }
    if (path == "/api/actions") { status = 200; const juce::ScopedLock sl (stateLock); return actionsJson; }
    if (method != "POST") { status = 400; return R"({"error":"POST attendu"})"; }

    if (path == "/api/action")
    {
        const auto id = query["id"];
        if (! isAllowedAction (id) || ctx.findAction (id) == nullptr) { status = 404; return R"({"error":"commande inconnue"})"; }
        juce::MessageManager::callAsync ([this, id, w = std::weak_ptr<int> (alive)] { if (w.expired()) return; ctx.perform (id); if (ctx.setStatus) ctx.setStatus (U ("Télécommande : ") + ctx.findAction (id)->name); });
        status = 200;
        return R"({"ok":true})";
    }
    if (path == "/api/track")
    {
        const int index = query["i"].getIntValue();
        const auto op = query["op"];
        if (! juce::StringArray { "mute", "solo", "arm", "select" }.contains (op)) { status = 400; return R"({"error":"opération inconnue"})"; }
        juce::MessageManager::callAsync ([this, index, op, w = std::weak_ptr<int> (alive)]
        {
            if (w.expired() || ctx.edit == nullptr) return;
            auto* t = te::getAudioTracks (*ctx.edit)[index];
            if (t == nullptr) return;
            if (op == "mute")   t->setMute (! t->isMuted (false));
            if (op == "solo")   t->setSolo (! t->isSolo (false));
            if (op == "arm")    ctx.setTrackArmed (*t, ! ctx.isTrackArmed (*t));
            if (op == "select") ctx.selectTrack (t);
            ctx.changed();
        });
        status = 200;
        return R"({"ok":true})";
    }
    status = 404;
    return R"({"error":"introuvable"})";
}
