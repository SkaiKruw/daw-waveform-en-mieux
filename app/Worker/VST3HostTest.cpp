// Test : un vrai VST3 chargé dans onde-plugin-host, puis un vrai plantage dans le code du VST3.
//   onde-vst3-test <onde-plugin-host> <Onde Test Synth.vst3> <Onde Test Crash.vst3>
#include "onde/PluginSandbox.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace onde;
static int failures = 0;
static void check (bool ok, const char* m) { std::printf ("  %s : %s\n", ok ? "ok    " : "ÉCHEC ", m); if (! ok) ++failures; }

int main (int argc, char** argv)
{
    if (argc < 4) { std::printf ("usage: onde-vst3-test <host> <synth.vst3> <crash.vst3>\n"); return 2; }
    const uint32_t n = 512; const double sr = 48000;
    std::vector<float> L (n), R (n); float* io[] = { L.data(), R.data() };
    auto energy = [&] { double e = 0; for (float v : L) e += std::fabs (v); return e; };

    std::printf ("\nVST3 dans un processus isolé\n");
    PluginSandbox synth ({ argv[1], std::string ("vst3:") + argv[2], 40, 0.9 });
    check (synth.start (sr, 2) && synth.waitUntilReady (15000), "le synthé VST3 se charge dans onde-plugin-host");
    const auto info = synth.getInfo();
    std::printf ("         nom : %s, instrument : %s, éditeur : %s\n", info.name.c_str(), info.isInstrument ? "oui" : "non", info.hasEditor ? "oui" : "non");
    check (info.isInstrument && info.hasEditor, "il est reconnu comme instrument avec une fenêtre d'édition");

    MidiEvent on { 0, 3, { 0x90, 57, 100 } };
    double total = 0;
    for (int b = 0; b < 40; ++b)
    {
        std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
        synth.process (io, 2, n, (uint64_t) b * n, b == 0 ? &on : nullptr, b == 0 ? 1u : 0u);
        total += energy();
    }
    check (total > 10.0, "une note MIDI envoyée au VST3 produit du son");

    std::vector<uint8_t> state;
    check (synth.saveState (state) && state.size() >= 4, "l'état du VST3 (préréglage) est récupéré");

    std::printf ("\nVrai plantage dans le code d'un VST3\n");
    PluginSandbox fx ({ argv[1], std::string ("vst3:") + argv[3], 40, 0.9 });
    check (fx.start (sr, 2) && fx.waitUntilReady (15000), "l'effet VST3 « Onde Test Crash » se charge");
    std::fill (L.begin(), L.end(), 1.0f); std::fill (R.begin(), R.end(), 1.0f);
    fx.process (io, 2, n, 0);
    check (std::fabs (L[0] - 0.7f) < 1e-3f, "il applique son volume (0,7)");

    // Un échantillon valant 12345 fait planter « Onde Test Crash » dans son propre processBlock.
    L[0] = 12345.0f;
    fx.process (io, 2, n, 0);
    for (int b = 0; b < 20 && fx.getState() == PluginSandbox::State::running; ++b)
        fx.process (io, 2, n, (uint64_t) b * n);
    check (fx.getState() == PluginSandbox::State::crashed, "le VST3 a planté dans son code");
    std::printf ("         cause affichée : %s\n", fx.describeFailure().c_str());

    // Le synthé, lui, joue toujours.
    total = 0;
    MidiEvent on2 { 0, 3, { 0x90, 60, 100 } };
    for (int b = 0; b < 20; ++b)
    {
        std::fill (L.begin(), L.end(), 0.0f);
        synth.process (io, 2, n, (uint64_t) (40 + b) * n, b == 0 ? &on2 : nullptr, b == 0 ? 1u : 0u);
        total += energy();
    }
    check (synth.getState() == PluginSandbox::State::running && total > 5.0, "le synthé VST3 voisin joue toujours");

    check (fx.restart() && fx.getState() == PluginSandbox::State::running, "« Relancer » redémarre l'effet");
    std::fill (L.begin(), L.end(), 1.0f); std::fill (R.begin(), R.end(), 1.0f);
    fx.process (io, 2, n, 0);
    check (std::fabs (L[0] - 0.7f) < 1e-3f, "après redémarrage, l'effet traite de nouveau le son");

    synth.stop(); fx.stop();
    std::printf ("\n%s\n", failures == 0 ? "VST3 OK — un vrai plugin qui plante ne coupe que sa piste." : "DES TESTS ÉCHOUENT");
    return failures == 0 ? 0 : 1;
}
