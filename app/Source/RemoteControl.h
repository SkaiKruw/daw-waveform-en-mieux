// Télécommande d'Onde depuis un téléphone (« mode Stream Deck ») : petit serveur HTTP sur le réseau local.
// Le téléphone ouvre http://<ip du PC>:<port>/?k=<code> (QR code affiché dans Onde) : une grille de gros
// boutons déclenche les commandes d'Onde et affiche l'état (position, tempo, lecture, pistes).
//
// Sécurité : désactivée par défaut ; chaque requête de l'API exige le code d'appairage à 6 chiffres
// (nouveau à chaque activation) ; 10 codes faux bloquent l'API 30 s ; aucune commande de fichier qui
// ouvre une fenêtre ou quitte Onde n'est exposée.
#pragma once
#include "EditContext.h"

class RemoteControl  : private juce::Thread, private juce::Timer
{
public:
    explicit RemoteControl (EditContext&);
    ~RemoteControl() override;

    bool start (int port = defaultPort, bool localOnly = false);   // false si le port est occupé ; localOnly : 127.0.0.1 (tests)
    void stop();
    bool isRunning() const                    { return isThreadRunning(); }
    int getPort() const                       { return port; }
    // Un téléphone a interrogé la télécommande (code valide) il y a moins de 3 s.
    bool phoneConnected() const               { return isRunning() && juce::Time::getMillisecondCounter() - lastClientMs.load() < 3000; }
    juce::String getCode() const              { return code; }

    struct Address { juce::String url, label; };
    juce::Array<Address> getAddresses() const;   // une par interface réseau IPv4 (réseau local, Tailscale…)

    // Actions accessibles depuis le téléphone (les commandes de fenêtre/fichier sont exclues).
    static bool isAllowedAction (const juce::String& id);

    static constexpr int defaultPort = 8765;
    std::function<void()> onStateChanged;     // pour rafraîchir la fenêtre de la télécommande

    // Pour les tests : traite une requête comme si elle venait du réseau (méthode, cible « /chemin?requête »).
    juce::String handleForTest (const juce::String& method, const juce::String& target, int& status);

private:
    void run() override;
    void timerCallback() override;
    void serve (juce::StreamingSocket&);
    juce::String handle (const juce::String& method, const juce::String& target, int& status, juce::String& contentType);
    bool checkCode (const juce::StringPairArray& query, int& status);
    juce::String buildState() const;
    juce::String buildActions() const;
    static juce::StringPairArray parseQuery (const juce::String&);

    EditContext& ctx;
    int port = defaultPort;
    juce::String code;
    std::unique_ptr<juce::StreamingSocket> listener;

    juce::CriticalSection stateLock;
    juce::String stateJson { "{}" }, actionsJson { "[]" };
    std::shared_ptr<int> alive = std::make_shared<int> (0);   // les appels différés vérifient que la télécommande existe encore
    std::atomic<juce::uint32> lastClientMs { 0 };
    int badAttempts = 0;
    juce::uint32 lockedUntil = 0;
};
