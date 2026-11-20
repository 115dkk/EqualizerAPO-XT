<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="de">
<context>
    <name>DeviceSelector</name>
    <message>
        <location filename="../DeviceSelector.cpp" line="+195"/>
        <source>Multi-core convolution processing</source>
        <translation>Faltung auf mehreren CPU-Kernen</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Spreads the work of Convolution and MultiConvolution over several CPU cores, so long impulse responses on many channels no longer load a single core. The sound and the latency stay the same.

It is optional because it starts extra audio threads. Turn it off if it causes dropouts on your system. Applies to every device.</source>
        <translation>Verteilt die Berechnungen von Convolution und MultiConvolution auf mehrere CPU-Kerne, damit lange Impulsantworten auf vielen Kanälen nicht mehr einen einzelnen Kern belasten. Klang und Latenz bleiben unverändert.

Die Funktion ist optional, da sie zusätzliche Audio-Threads startet. Deaktivieren Sie sie, wenn sie auf Ihrem System Tonaussetzer verursacht. Gilt für alle Geräte.</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Resample received audio</source>
        <translation>Empfangene Audiosignale neu abtasten</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Converts audio that Send: lines in other endpoints&apos; configurations hand to this endpoint when their sample rate differs from this endpoint&apos;s, and keeps the two clocks aligned. Without it, a sender at another sample rate is refused. At equal sample rates nothing changes.

It is optional because while it converts it uses CPU and a little latency, and the result is no longer bit-identical to what was sent.</source>
        <translation>Wandelt die Audiosignale um, die Send:-Zeilen in den Konfigurationen anderer Endpunkte an diesen Endpunkt senden, wenn sich deren Abtastrate von seiner unterscheidet. Dabei werden die beiden Takte synchron gehalten. Ohne diese Option wird ein Sender mit einer anderen Abtastrate abgewiesen. Bei gleicher Abtastrate ändert sich nichts.

Die Funktion ist optional, weil die Umwandlung Rechenleistung benötigt und etwas Latenz hinzufügt. Das Ergebnis stimmt zudem nicht mehr Bit für Bit mit dem gesendeten Signal überein.</translation>
    </message>
    <message>
        <location line="-24"/>
        <source>Ignore silence in convolution</source>
        <translation>Stille bei der Faltung ignorieren</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cuts the silent end off impulse responses that Convolution and MultiConvolution load. A long silent tail costs as much processing as sound does and can cause dropouts. Applies to every device.</source>
        <translation>Entfernt die Stille am Ende der Impulsantworten, die Convolution und MultiConvolution laden. Stille benötigt genauso viel Rechenleistung wie hörbares Audio. Ein langer stiller Abschnitt am Ende kann daher zu Tonaussetzern führen. Gilt für alle Geräte.</translation>
    </message>
    <message>
        <location line="-119"/>
        <location line="+44"/>
        <source>Playback devices</source>
        <translation>Wiedergabegeräte</translation>
    </message>
    <message>
        <location line="-39"/>
        <location line="+43"/>
        <source>Capture devices</source>
        <translation>Aufnahmegeräte</translation>
    </message>
    <message>
        <location line="-35"/>
        <location line="+17"/>
        <location line="+342"/>
        <location line="+13"/>
        <source>Error while accessing the registry</source>
        <translation>Fehler beim Zugriff auf die Registry</translation>
    </message>
    <message>
        <location line="-360"/>
        <location line="+462"/>
        <source>Info</source>
        <translation>Info</translation>
    </message>
    <message>
        <location line="-462"/>
        <source>A registry value that is required for the operation of Equalizer APO was not set correctly. This might have been caused by a driver installation or uninstallation. The value has been corrected. A reboot may be required so that the changes can take effect.</source>
        <translation>Ein Registrywert, der für den Betrieb von Equalizer APO erforderlich ist, war nicht korrekt gesetzt. Dies könnte durch eine Treiberinstallation oder -deinstallation ausgelöst worden sein. Der Wert ist nun korrigiert. Ein Neustart kann erforderlich sein, damit die Änderungen wirksam werden.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The registry repair did not finish. Reinstall Equalizer APO or see DeviceSelector.log for details.</source>
        <translation>Die Reparatur der Registry wurde nicht abgeschlossen. Installieren Sie Equalizer APO neu oder sehen Sie in DeviceSelector.log nach Einzelheiten.</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>OK</source>
        <translation>OK</translation>
    </message>
    <message>
        <source>Undoing the change did not finish either, so this device may be left partly changed. Reboot before trying again, and see the log for details.</source>
        <translation type="vanished">Auch das Rückgängigmachen der Änderung wurde nicht abgeschlossen. Das Gerät kann daher noch teilweise geändert sein. Starten Sie den Computer vor einem erneuten Versuch neu. Einzelheiten finden Sie im Protokoll.</translation>
    </message>
    <message>
        <source>The device was left as it was before.</source>
        <translation type="vanished">Das Gerät ist im vorherigen Zustand.</translation>
    </message>
    <message>
        <source>Details are in %1.</source>
        <translation type="vanished">Einzelheiten finden Sie in %1.</translation>
    </message>
    <message>
        <location line="+761"/>
        <source>APO will be uninstalled</source>
        <translation>APO wird deinstalliert</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>APO will be upgraded</source>
        <translation>APO wird aktualisiert</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>APO installation will be changed</source>
        <translation>APO-Installation wird geändert</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Audio enhancements will be enabled</source>
        <translation>Signalverbesserungen werden aktiviert</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>APO is already installed</source>
        <translation>APO ist bereits installiert</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>APO can be installed</source>
        <translation>APO kann installiert werden</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Voicemeeter was uninstalled</source>
        <translation>Voicemeeter wurde deinstalliert</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Default device</source>
        <translation>Standardgerät</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Disabled</source>
        <translation>Deaktiviert</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Unplugged</source>
        <translation>Nicht angeschlossen</translation>
    </message>
    <message>
        <location line="-417"/>
        <source>Upgrades available</source>
        <translation>Aktualisierungen verfügbar</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>The APO installation of some devices should be upgraded. Do you really want to cancel?</source>
        <translation>Die APO-Installation einiger Geräte sollte aktualisiert werden. Möchten Sie wirklich abbrechen?</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Reboot</source>
        <translation>Neustarten</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>To apply the changes, Windows should be rebooted. Reboot now?</source>
        <translation>Um die Änderungen anzuwenden, sollte Windows neugestartet werden. Jetzt neustarten?</translation>
    </message>
    <message>
        <location line="+358"/>
        <source>APO will be installed</source>
        <translation>APO wird installiert</translation>
    </message>
    <message>
        <location line="-757"/>
        <location line="+577"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Close</source>
        <translation>Schließen</translation>
    </message>
    <message>
        <location line="-189"/>
        <source>This dialog can be reopened anytime by launching Device Selector from the start menu.</source>
        <translation>Dieser Dialog kann jederzeit erneut geöffnet werden, indem Device Selector aus dem Startmenü gestartet wird.</translation>
    </message>
</context>
<context>
    <name>DeviceSelectorClass</name>
    <message>
        <location filename="../DeviceSelector.ui" line="+176"/>
        <source>Use in ASIO apps</source>
        <translation>In ASIO-Anwendungen verwenden</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Adds `&lt;device&gt; - &lt;endpoint&gt; (EQ APO XT)` to the ASIO driver list. An application that picks it opens this device in WASAPI exclusive mode with the EQ applied.</source>
        <translation>Fügt der ASIO-Treiberliste `&lt;Gerät&gt; - &lt;Endpunkt&gt; (EQ APO XT)` hinzu. Eine Anwendung, die ihn wählt, öffnet dieses Gerät im exklusiven WASAPI-Modus mit angewendetem EQ.</translation>
    </message>
    <message>
        <source>Connector</source>
        <translation type="vanished">Anschluss</translation>
    </message>
    <message>
        <source>Device</source>
        <translation type="vanished">Gerät</translation>
    </message>
    <message>
        <source>Status</source>
        <translation type="vanished">Status</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="-390"/>
        <source>Troubleshooting options (only use in case of problems)</source>
        <translation>Problemlösungsoptionen (nur im Fall von Problemen verwenden)</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="-110"/>
        <location line="+81"/>
        <source>Use original APO</source>
        <translation>Original-APO verwenden</translation>
    </message>
    <message>
        <location line="-88"/>
        <location line="+30"/>
        <source>Install APO</source>
        <translation>APO installieren</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Post-mix:</source>
        <translation>Post-mix:</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Install as LFX/GFX</source>
        <translation>Als LFX/GFX installieren</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Install as SFX/MFX</source>
        <translation>Als SFX/MFX installieren</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Install as SFX/EFX</source>
        <translation>Als SFX/EFX installieren</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Automatically adjust install mode if problems with current mode are detected</source>
        <translation>Installationsmodus automatisch anpassen, wenn Probleme mit dem aktuellen Modus erkannt werden</translation>
    </message>
    <message>
        <location line="-75"/>
        <source>Pre-mix:</source>
        <translation>Pre-mix:</translation>
    </message>
    <message>
        <location line="+78"/>
        <source>Auto:</source>
        <translation>Auto:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Allows to change silent input buffers to audible output buffers, so that delayed audio can be played.

CAUTION: Some sound card drivers can produce unwanted sounds when this is enabled.</source>
        <translation>Erlaubt die Änderung stiller Eingangspuffer zu hörbaren Ausgangspuffern, was die Wiedergabe verzögerter Töne ermöglicht.

ACHTUNG: Einige Soundkartentreiber erzeugen unerwünschte Geräusche, wenn diese Einstellung aktiv ist.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Allow silent buffer modification</source>
        <translation>Erlaube Modifikation stiller Puffer</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Plays what Send: lines in other endpoints&apos; configurations hand to this endpoint, even when no program plays here. EqualizerAPOHost keeps a silent stream open on it from logon, and silent buffer modification is turned on with it.</source>
        <translation>Gibt die Audiosignale wieder, die Send:-Zeilen in den Konfigurationen anderer Endpunkte an diesen Endpunkt senden, auch wenn kein Programm hier etwas abspielt. EqualizerAPOHost hält ab der Anmeldung einen stillen Audiostream auf diesem Endpunkt offen. Diese Option erlaubt zugleich die Modifikation stiller Puffer.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Send</source>
        <translation>Senden</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Remove the buffer</source>
        <translation>Puffer entfernen</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Wait time:</source>
        <translation>Wartezeit:</translation>
    </message>
    <message>
        <location line="-3"/>
        <location line="+10"/>
        <source>Sets how long to wait for processing once the buffer is removed. The longer the wait, the less sound without the EQ, but the more latency.</source>
        <translation>Legt fest, wie lange nach dem Entfernen des Puffers auf die Verarbeitung gewartet wird. Je länger gewartet wird, desto seltener kommt Ton ohne EQ, aber desto höher die Latenz.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Up to a quarter of the buffer</source>
        <translation>Bis zu einem Viertel des Puffers</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Up to half of the buffer</source>
        <translation>Bis zur Hälfte des Puffers</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Up to three quarters of the buffer</source>
        <translation>Bis zu drei Vierteln des Puffers</translation>
    </message>
    <message>
        <location line="-50"/>
        <source>Removing the buffer reduces latency. But when processing occasionally does not finish in time, sound without the EQ applied may come out for a moment. Applies to both input and output.</source>
        <translation>Ohne den Puffer sinkt die Latenz. Wird die Verarbeitung aber gelegentlich nicht rechtzeitig fertig, kann kurzzeitig Ton ohne EQ ausgegeben werden. Gilt für Eingang und Ausgang.</translation>
    </message>
    <message>
        <location line="+81"/>
        <source>Start the engine host automatically at boot</source>
        <translation>Engine-Host beim Systemstart automatisch starten</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Unchecked, the first application that opens the driver starts the host, and it leaves a minute after the last one closes it.</source>
        <translation>Deaktiviert startet die erste Anwendung, die den Treiber öffnet, den Host; eine Minute nachdem die letzte ihn geschlossen hat, beendet er sich.</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>32-bit host support</source>
        <translation>Unterstützung für 32-Bit-Hosts</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Also registers the driver entry where 32-bit applications look for it. Unavailable when this build ships no 32-bit wrapper.</source>
        <translation>Registriert den Treibereintrag auch dort, wo 32-Bit-Anwendungen ihn suchen. Nicht verfügbar, wenn dieser Build keinen 32-Bit-Wrapper enthält.</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Copy Device command to clipboard</source>
        <translation>Kopiere Device-Befehl in die Zwischenablage</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Please select the devices for which Equalizer APO is to be installed:</source>
        <translation>Bitte die Geräte auswählen, für die Equalizer APO installiert werden soll:</translation>
    </message>
    <message>
        <location line="-340"/>
        <source>Please select exactly one activated device by clicking on its name</source>
        <translation>Bitte genau ein aktiviertes Gerät durch Klick auf den Namen wählen</translation>
    </message>
</context>
<context>
    <name>DeviceTestDialog</name>
    <message>
        <source>Playback devices</source>
        <translation type="vanished">Wiedergabegeräte</translation>
    </message>
    <message>
        <source>Capture devices</source>
        <translation type="vanished">Aufnahmegeräte</translation>
    </message>
    <message>
        <source>Error while accessing the registry</source>
        <translation type="vanished">Fehler beim Zugriff auf die Registry</translation>
    </message>
    <message>
        <source>Cannot test APO installation as device is disabled</source>
        <translation type="vanished">Kann APO-Installation nicht testen, da Gerät deaktiviert ist</translation>
    </message>
    <message>
        <source>Cannot test APO installation as device is unplugged</source>
        <translation type="vanished">Kann APO-Installation nicht testen, da Gerät nicht angeschlossen ist</translation>
    </message>
    <message>
        <source>Error</source>
        <translation type="vanished">Fehler</translation>
    </message>
    <message>
        <source>Equalizer APO works but original APO could not be initialized. Maybe unset &quot;Use original APO&quot; in troubleshooting options.</source>
        <translation type="vanished">Equalizer APO funktioniert, aber das Original-APO konnte nicht initialisiert werden. Eventuell &quot;Original-APO verwenden&quot; in den Problemlösungsoptionen deaktivieren.</translation>
    </message>
</context>
<context>
    <name>DeviceTestDialogClass</name>
    <message>
        <source>Testing APO installation on devices...</source>
        <translation type="vanished">Teste APO-Installation auf Geräten...</translation>
    </message>
    <message>
        <source>Connector</source>
        <translation type="vanished">Anschluss</translation>
    </message>
    <message>
        <source>Device</source>
        <translation type="vanished">Gerät</translation>
    </message>
    <message>
        <source>Pre-mix</source>
        <translation type="vanished">Pre-mix</translation>
    </message>
    <message>
        <source>Post-mix</source>
        <translation type="vanished">Post-mix</translation>
    </message>
</context>
<context>
    <name>DeviceTestThread</name>
    <message>
        <location filename="../DeviceTestThread.cpp" line="+82"/>
        <source>Could not initialize COM for device testing.</source>
        <translation>COM konnte für den Gerätetest nicht initialisiert werden.</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+2"/>
        <source>COM initialization failed (0x%1).</source>
        <translation>COM-Initialisierung fehlgeschlagen (0x%1).</translation>
    </message>
    <message>
        <location line="+27"/>
        <location line="+190"/>
        <source>Restarting audio service...</source>
        <translation>Starte Audio-Dienst neu...</translation>
    </message>
    <message>
        <location line="-185"/>
        <location line="+190"/>
        <source>Restart failed.</source>
        <translation>Neustart fehlgeschlagen.</translation>
    </message>
    <message>
        <location line="-175"/>
        <source>Checking APO installation...</source>
        <translation>Überprüfe APO-Installation...</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Could not prepare the device test.</source>
        <translation>Der Gerätetest konnte nicht vorbereitet werden.</translation>
    </message>
    <message>
        <location line="-38"/>
        <location line="+50"/>
        <source>Could not remove the device test registration: %1</source>
        <translation>Die Registrierung für den Gerätetest konnte nicht entfernt werden: %1</translation>
    </message>
    <message>
        <location line="+56"/>
        <source>Received unknown device GUID %1.</source>
        <translation>Unbekannte Geräte-GUID %1 empfangen.</translation>
    </message>
    <message numerus="yes">
        <location line="+23"/>
        <location filename="../DeviceTestView.cpp" line="+512"/>
        <source>Check failed for %n device(s).</source>
        <translation>
            <numerusform>Überprüfung fehlgeschlagen für %n Gerät.</numerusform>
            <numerusform>Überprüfung fehlgeschlagen für %n Geräte.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Setting install mode for %1 %2 to %3.</source>
        <translation>Setze Installationsmodus für %1 %2 auf %3.</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Trying other configurations...</source>
        <translation>Versuche andere Konfigurationen...</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Checks done. No problems were detected.</source>
        <translation>Überprüfungen abgeschlossen. Es wurden keine Probleme erkannt.</translation>
    </message>
    <message numerus="yes">
        <location line="+2"/>
        <location filename="../DeviceTestView.cpp" line="+1"/>
        <source>Checks done. Problems were detected for %n device(s).</source>
        <translation>
            <numerusform>Überprüfungen abgeschlossen. Für %n Gerät wurden Probleme erkannt.</numerusform>
            <numerusform>Überprüfungen abgeschlossen. Für %n Geräte wurden Probleme erkannt.</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>DeviceTestView</name>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="+71"/>
        <source>Reinstalling %1 %2</source>
        <translation>APO für %1 %2 wird neu installiert</translation>
    </message>
    <message>
        <location line="+80"/>
        <location line="+204"/>
        <location filename="../DeviceTestView.cpp" line="-47"/>
        <source>Checking</source>
        <translation>Überprüfung läuft</translation>
    </message>
    <message>
        <location line="-191"/>
        <source>Details are in %1.</source>
        <translation>Einzelheiten finden Sie in %1.</translation>
    </message>
    <message>
        <location line="+168"/>
        <location line="+42"/>
        <location filename="../DeviceTestView.cpp" line="-1"/>
        <source>The APO works</source>
        <translation>APO funktioniert</translation>
    </message>
    <message>
        <location line="-34"/>
        <source>Trying another install mode</source>
        <translation>Anderer Installationsmodus wird getestet</translation>
    </message>
    <message>
        <location line="+5"/>
        <location filename="../DeviceTestView.cpp" line="+12"/>
        <source>Pre-mix works. Checking post-mix</source>
        <translation>Pre-mix funktioniert. Post-mix wird geprüft</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Post-mix works. Checking pre-mix</source>
        <translation>Post-mix funktioniert. Pre-mix wird geprüft</translation>
    </message>
    <message>
        <location line="+71"/>
        <location filename="../DeviceTestView.cpp" line="+58"/>
        <source>Windows audio</source>
        <translation>Windows-Audio</translation>
    </message>
    <message>
        <location line="+82"/>
        <location filename="../DeviceTestView.cpp" line="-8"/>
        <source>Not checked</source>
        <translation>Nicht geprüft</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="-440"/>
        <source>APO installation check</source>
        <translation>APO-Installation prüfen</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Record</source>
        <translation>Protokoll</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Stop</source>
        <translation>Stoppen</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Reinstall</source>
        <translation>Neu installieren</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Close</source>
        <translation>Schließen</translation>
    </message>
    <message>
        <location line="+79"/>
        <location line="+4"/>
        <source>The APO does not work</source>
        <translation>APO funktioniert nicht</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>All three install modes were tried and Windows did not load the APO on this device in any of them.</source>
        <translation>Alle drei Installationsmodi wurden getestet. Windows hat das APO bei diesem Gerät in keinem davon geladen.</translation>
    </message>
    <message>
        <source>The cause could not be determined. Update the device&apos;s driver and check again; some drivers load no APO at all.</source>
        <translation type="vanished">Die Ursache konnte nicht ermittelt werden. Aktualisieren Sie den Gerätetreiber und prüfen Sie erneut. Manche Treiber laden grundsätzlich kein APO.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Windows did not load the APO on this device in the selected install mode (%1).</source>
        <translation>Windows hat das APO bei diesem Gerät im gewählten Installationsmodus (%1) nicht geladen.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Turn on Automatic in the troubleshooting options, and the check tries the other modes too.</source>
        <translation>Aktivieren Sie Auto in den Problemlösungsoptionen, damit auch die anderen Modi getestet werden.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Pre-mix works, post-mix does not</source>
        <translation>Pre-mix funktioniert, Post-mix nicht</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The pre-mix APO works. Windows did not load the post-mix APO on this device.</source>
        <translation>Das Pre-mix-APO funktioniert. Windows hat das Post-mix-APO bei diesem Gerät nicht geladen.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Filters of the post-mix stage are not applied. Turn off post-mix install for this device, or try another install mode.</source>
        <translation>Die Filter der Post-mix-Stufe werden nicht angewendet. Deaktivieren Sie die Post-mix-Installation für dieses Gerät oder versuchen Sie einen anderen Installationsmodus.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Post-mix works, pre-mix does not</source>
        <translation>Post-mix funktioniert, Pre-mix nicht</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The post-mix APO works. Windows did not load the pre-mix APO on this device.</source>
        <translation>Das Post-mix-APO funktioniert. Windows hat das Pre-mix-APO bei diesem Gerät nicht geladen.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Filters of the pre-mix stage are not applied. Try another install mode.</source>
        <translation>Die Filter der Pre-mix-Stufe werden nicht angewendet. Versuchen Sie einen anderen Installationsmodus.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The APO works, but the original APO could not start</source>
        <translation>APO funktioniert, aber das Original-APO konnte nicht starten</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The equalizer works. The APO of the device&apos;s maker, which runs beside it, did not start, so that maker&apos;s effects are missing.</source>
        <translation>Der Equalizer funktioniert. Das APO des Geräteherstellers, das zusätzlich läuft, ist nicht gestartet. Die Effekte des Herstellers fehlen daher.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>If you do not use those effects, turn off Use original APO in the troubleshooting options.</source>
        <translation>Wenn Sie diese Effekte nicht nutzen, deaktivieren Sie &quot;Original-APO verwenden&quot; in den Problemlösungsoptionen.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The install mode could not be changed</source>
        <translation>Installationsmodus konnte nicht geändert werden</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The check tried to install in another mode and Windows refused the change. The device was left as it was before.</source>
        <translation>Bei der Überprüfung wurde eine Installation in einem anderen Modus versucht. Windows hat die Änderung verweigert. Das Gerät ist im vorherigen Zustand.</translation>
    </message>
    <message>
        <source>Close programs that manage audio devices, then check again.</source>
        <translation type="vanished">Schließen Sie Programme zur Verwaltung von Audiogeräten und prüfen Sie erneut.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Not changed</source>
        <translation>Nicht geändert</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Windows refused the change to this device. The device was left as it was before.</source>
        <translation>Windows hat die Änderung an diesem Gerät verweigert. Das Gerät ist im vorherigen Zustand.</translation>
    </message>
    <message>
        <source>Close programs that manage audio devices, then try again.</source>
        <translation type="vanished">Schließen Sie Programme zur Verwaltung von Audiogeräten und versuchen Sie es erneut.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Partly changed</source>
        <translation>Teilweise geändert</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Windows refused the change, and undoing it did not finish either. The device may be left partly changed.</source>
        <translation>Windows hat die Änderung verweigert. Auch das Rückgängigmachen wurde nicht abgeschlossen. Das Gerät kann daher noch teilweise geändert sein.</translation>
    </message>
    <message>
        <source>Restart the computer before trying again.</source>
        <translation type="vanished">Starten Sie den Computer vor einem erneuten Versuch neu.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Not checked: the device is disabled</source>
        <translation>Nicht geprüft: Gerät deaktiviert</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The device is disabled in Windows, so nothing could be played through it.</source>
        <translation>Das Gerät ist in Windows deaktiviert. Daher konnte darüber kein Ton wiedergegeben werden.</translation>
    </message>
    <message>
        <source>Enable the device, then open the Device Selector and press OK to check again.</source>
        <translation type="vanished">Aktivieren Sie das Gerät. Öffnen Sie dann Device Selector und klicken Sie auf OK, um erneut zu prüfen.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Not checked: the device is unplugged</source>
        <translation>Nicht geprüft: Gerät nicht angeschlossen</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing is plugged into this device, so nothing could be played through it.</source>
        <translation>An diesem Gerät ist nichts angeschlossen. Daher konnte darüber kein Ton wiedergegeben werden.</translation>
    </message>
    <message>
        <source>Plug it in, then open the Device Selector and press OK to check again.</source>
        <translation type="vanished">Stellen Sie die Verbindung her. Öffnen Sie dann Device Selector und klicken Sie auf OK, um erneut zu prüfen.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Not checked: the device could not be opened</source>
        <translation>Nicht geprüft: Gerät konnte nicht geöffnet werden</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing could be played through this device, so the APO on it could not answer.</source>
        <translation>Über dieses Gerät konnte kein Ton wiedergegeben werden. Daher konnte das APO darauf nicht antworten.</translation>
    </message>
    <message>
        <source>Open the Device Selector and press OK to check again.</source>
        <translation type="vanished">Öffnen Sie Device Selector und klicken Sie auf OK, um erneut zu prüfen.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Windows audio could not be restarted, so the check did not run. The APO is installed, but it starts working only after Windows audio restarts.</source>
        <translation>Windows-Audio konnte nicht neu gestartet werden. Daher fand keine Überprüfung statt. Das APO ist installiert, funktioniert aber erst nach einem Neustart von Windows-Audio.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Restart the computer.</source>
        <translation>Starten Sie den Computer neu.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>A Windows component the check needs did not start, so the check did not run.</source>
        <translation>Eine für die Überprüfung benötigte Windows-Komponente ist nicht gestartet. Daher fand keine Überprüfung statt.</translation>
    </message>
    <message>
        <location line="-19"/>
        <location line="+20"/>
        <source>Restart the computer, then open the Device Selector and press OK.</source>
        <translation>Starten Sie den Computer neu. Öffnen Sie dann Device Selector und klicken Sie auf OK.</translation>
    </message>
    <message>
        <location line="-48"/>
        <source>The cause could not be determined. Update the device&apos;s driver, then press Reinstall. Some drivers load no APO at all.</source>
        <translation>Die Ursache konnte nicht ermittelt werden. Aktualisieren Sie den Gerätetreiber und klicken Sie dann auf &quot;Neu installieren&quot;. Manche Treiber laden grundsätzlich kein APO.</translation>
    </message>
    <message>
        <location line="+20"/>
        <location line="+4"/>
        <source>Close programs that manage audio devices, then press Reinstall.</source>
        <translation>Schließen Sie Programme zur Verwaltung von Audiogeräten und klicken Sie dann auf &quot;Neu installieren&quot;.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Enable the device, then press Reinstall.</source>
        <translation>Aktivieren Sie das Gerät und klicken Sie dann auf &quot;Neu installieren&quot;.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Plug it in, then press Reinstall.</source>
        <translation>Stellen Sie die Verbindung her und klicken Sie dann auf &quot;Neu installieren&quot;.</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+16"/>
        <location line="+4"/>
        <location line="+4"/>
        <source>Press Reinstall to try again.</source>
        <translation>Klicken Sie auf &quot;Neu installieren&quot;, um es erneut zu versuchen.</translation>
    </message>
    <message>
        <location line="-13"/>
        <source>Windows blocked the setting the check uses to hear from the APO, so the check did not run.</source>
        <translation>Windows hat die Einstellung blockiert, die zum Empfangen der APO-Antworten nötig ist. Daher fand keine Überprüfung statt.</translation>
    </message>
    <message>
        <source>Close security programs that block changes to system settings, then open the Device Selector and press OK.</source>
        <translation type="vanished">Schließen Sie Sicherheitsprogramme, die Änderungen an Systemeinstellungen blockieren. Öffnen Sie dann Device Selector und klicken Sie auf OK.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close security programs that block changes to system settings, then press Reinstall.</source>
        <translation>Schließen Sie Sicherheitsprogramme, die Änderungen an Systemeinstellungen blockieren. Klicken Sie dann auf &quot;Neu installieren&quot;.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The check lost its connection to the APO before every device had answered.</source>
        <translation>Die Verbindung zum APO wurde unterbrochen, bevor alle Geräte geantwortet hatten.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The list of audio devices could not be read, so no device was checked.</source>
        <translation>Die Liste der Audiogeräte konnte nicht gelesen werden. Daher wurde kein Gerät geprüft.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The check was stopped. The APO is installed, but whether it works was not checked.</source>
        <translation>Die Überprüfung wurde gestoppt. Das APO ist installiert, aber seine Funktion wurde nicht geprüft.</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>Checking that the APO works on each device (%1 of %2)</source>
        <translation>APO-Funktion auf jedem Gerät wird geprüft (%1 von %2)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Restarting Windows audio</source>
        <translation>Windows-Audio wird neu gestartet</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Windows audio restarts during the check, so sound stops for a few seconds.</source>
        <translation>Während der Überprüfung wird Windows-Audio neu gestartet. Der Ton wird dabei für einige Sekunden unterbrochen.</translation>
    </message>
    <message numerus="yes">
        <location line="+6"/>
        <source>The APO does not work on %n device(s).</source>
        <translation>
            <numerusform>Das APO funktioniert auf %n Gerät nicht.</numerusform>
            <numerusform>Das APO funktioniert auf %n Geräten nicht.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The other devices are not affected.</source>
        <translation>Die anderen Geräte sind nicht betroffen.</translation>
    </message>
    <message numerus="yes">
        <location line="+5"/>
        <source>%n device(s) could not be changed.</source>
        <translation>
            <numerusform>%n Gerät konnte nicht geändert werden.</numerusform>
            <numerusform>%n Geräte konnten nicht geändert werden.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The APO works on the devices that were checked.</source>
        <translation>Das APO funktioniert auf den geprüften Geräten.</translation>
    </message>
    <message numerus="yes">
        <location line="+5"/>
        <source>The APO works. %n device(s) have something to look at.</source>
        <translation>
            <numerusform>Das APO funktioniert. Bei %n Gerät gibt es etwas zu prüfen.</numerusform>
            <numerusform>Das APO funktioniert. Bei %n Geräten gibt es etwas zu prüfen.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The APO is not installed on any device.</source>
        <translation>Das APO ist auf keinem Gerät installiert.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The APO works on every device that could be checked.</source>
        <translation>Das APO funktioniert auf allen Geräten, die geprüft werden konnten.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No device could be checked.</source>
        <translation>Kein Gerät konnte geprüft werden.</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>%n device(s) could not be checked.</source>
        <translation>
            <numerusform>%n Gerät konnte nicht geprüft werden.</numerusform>
            <numerusform>%n Geräte konnten nicht geprüft werden.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The APO works on every device.</source>
        <translation>Das APO funktioniert auf allen Geräten.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The equalizer is applied from now on.</source>
        <translation>Der Equalizer wird ab jetzt angewendet.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The check did not finish.</source>
        <translation>Die Überprüfung wurde nicht abgeschlossen.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Whether the APO works is not known.</source>
        <translation>Ob das APO funktioniert, ist nicht bekannt.</translation>
    </message>
</context>
</TS>
