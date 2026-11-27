#include "Updater.h"
#include "AboutPanel.h"
#include "Preferences.h"

namespace updates
{
    juce::String currentVersion() { return AboutPanel::version; }

    juce::String repository() { return prefs::file().getValue ("updateRepo", "SkaiKruw/onde"); }

    bool isNewer (const juce::String& candidate, const juce::String& current)
    {
        auto parts = [] (juce::String v)
        {
            v = v.trim().trimCharactersAtStart ("vV");
            juce::Array<int> r;
            for (auto& p : juce::StringArray::fromTokens (v, ".-", "")) r.add (p.getIntValue());
            while (r.size() < 3) r.add (0);
            return r;
        };
        const auto a = parts (candidate), b = parts (current);
        for (int i = 0; i < juce::jmax (a.size(), b.size()); ++i)
        {
            const int x = a[i], y = b[i];
            if (x != y) return x > y;
        }
        return false;
    }

    Info parseRelease (const juce::String& json)
    {
        Info info;
        const auto v = juce::JSON::parse (json);
        if (auto* o = v.getDynamicObject())
        {
            info.latest = o->getProperty ("tag_name").toString().trimCharactersAtStart ("vV");
            info.pageUrl = o->getProperty ("html_url").toString();
            info.notes = o->getProperty ("body").toString();
            info.ok = info.latest.isNotEmpty();
            info.newer = info.ok && isNewer (info.latest, currentVersion());
        }
        if (! info.ok) info.error = juce::String::fromUTF8 ("aucune version publiée trouvée");
        return info;
    }

    bool shouldCheckToday()
    {
        if (! prefs::file().getBoolValue ("checkUpdates", true)) return false;
        const auto last = juce::Time (prefs::file().getValue ("lastUpdateCheck", "0").getLargeIntValue());
        return (juce::Time::getCurrentTime() - last).inHours() >= 24.0;
    }

    void check (std::function<void (const Info&)> done)
    {
        const auto repo = repository();
        juce::Thread::launch ([repo, done]
        {
            Info info;
            const juce::URL url ("https://api.github.com/repos/" + repo + "/releases/latest");
            int status = 0;
            auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                               .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: Onde")
                               .withConnectionTimeoutMs (8000)
                               .withStatusCode (&status);
            if (auto stream = url.createInputStream (options))
            {
                if (status == 200) info = parseRelease (stream->readEntireStreamAsString());
                else info.error = status == 404 ? juce::String::fromUTF8 ("aucune version publiée sur ") + repo
                                                : "erreur " + juce::String (status);
            }
            else info.error = juce::String::fromUTF8 ("pas de connexion");
            juce::MessageManager::callAsync ([info, done]
            {
                prefs::file().setValue ("lastUpdateCheck", juce::String (juce::Time::getCurrentTime().toMilliseconds()));
                if (done) done (info);
            });
        });
    }
}
