<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="fr_FR">
<context>
    <name>DeviceSelector</name>
    <message>
        <location filename="../DeviceSelector.cpp" line="+196"/>
        <source>Multi-core convolution processing</source>
        <translation>Traitement de la convolution sur plusieurs cœurs</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Spreads the work of Convolution and MultiConvolution over several CPU cores, so long impulse responses on many channels no longer load a single core. The sound and the latency stay the same.

It is optional because it starts extra audio threads. Turn it off if it causes dropouts on your system. Applies to every device.</source>
        <translation>Répartit les calculs de Convolution et MultiConvolution sur plusieurs cœurs du processeur, pour que les longues réponses impulsionnelles sur de nombreux canaux ne sollicitent plus un seul cœur. Le son et la latence restent inchangés.

Cette option est facultative, car elle lance des threads audio supplémentaires. Désactivez-la si elle provoque des coupures audio sur votre système. S’applique à tous les périphériques.</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Resample received audio</source>
        <translation>Rééchantillonner le son reçu</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Converts audio that Send: lines in other endpoints&apos; configurations hand to this endpoint when their sample rate differs from this endpoint&apos;s, and keeps the two clocks aligned. Without it, a sender at another sample rate is refused. At equal sample rates nothing changes.

It is optional because while it converts it uses CPU and a little latency, and the result is no longer bit-identical to what was sent.</source>
        <translation>Convertit le son envoyé à ce point de terminaison par les lignes Send: des configurations des autres points de terminaison lorsque leur fréquence d’échantillonnage diffère de la sienne, et maintient les deux horloges synchronisées. Sans cette option, un émetteur utilisant une autre fréquence d’échantillonnage est refusé. À fréquence égale, rien ne change.

Cette option est facultative, car la conversion utilise le processeur et ajoute un peu de latence. Le résultat n’est plus identique, bit pour bit, au son envoyé.</translation>
    </message>
    <message>
        <location line="-24"/>
        <source>Ignore silence in convolution</source>
        <translation>Ignorer le silence dans la convolution</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cuts the silent end off impulse responses that Convolution and MultiConvolution load. A long silent tail costs as much processing as sound does and can cause dropouts. Applies to every device.</source>
        <translation>Supprime le silence à la fin des réponses impulsionnelles chargées par Convolution et MultiConvolution. Le silence demande autant de calculs que le son. Une longue portion silencieuse à la fin peut donc provoquer des coupures audio. S’applique à tous les périphériques.</translation>
    </message>
    <message>
        <location line="-119"/>
        <location line="+44"/>
        <source>Playback devices</source>
        <translation>Périphériques de lecture</translation>
    </message>
    <message>
        <location line="-39"/>
        <location line="+43"/>
        <source>Capture devices</source>
        <translation>Périphériques de capture</translation>
    </message>
    <message>
        <location line="-35"/>
        <location line="+17"/>
        <location line="+349"/>
        <location line="+13"/>
        <source>Error while accessing the registry</source>
        <translation>Erreur durant l&apos;accès au registre</translation>
    </message>
    <message>
        <location line="-367"/>
        <location line="+487"/>
        <source>Info</source>
        <translation>Information</translation>
    </message>
    <message>
        <location line="-487"/>
        <source>A registry value that is required for the operation of Equalizer APO was not set correctly. This might have been caused by a driver installation or uninstallation. The value has been corrected. A reboot may be required so that the changes can take effect.</source>
        <translation>Une valeur de registre requise pour le fonctionnement d’Equalizer APO n’a pas été définie correctement. Cela peut être dû à l&apos;installation ou à la désinstallation d&apos;un pilote. La valeur a été corrigée. Un redémarrage peut être nécessaire pour que les modifications puissent prendre effet.</translation>
    </message>
    <message>
        <location line="+463"/>
        <source>Upgrades available</source>
        <translation>Mises à niveau disponibles</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>The APO installation of some devices should be upgraded. Do you really want to cancel?</source>
        <translation>L&apos;installation APO de certains appareils doit être mise à niveau. Voulez-vous vraiment annuler?</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>This dialog can be reopened anytime by launching Device Selector from the start menu.</source>
        <translation>Cette boîte de dialogue peut être rouverte à tout moment en lançant Device Selector à partir du menu Démarrer.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Reboot</source>
        <translation>Redémarrer</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>To apply the changes, Windows should be rebooted. Reboot now?</source>
        <translation>Pour appliquer les modifications, Windows doit être redémarré. Redémarrer maintenant?</translation>
    </message>
    <message>
        <location line="-424"/>
        <location line="+602"/>
        <source>Cancel</source>
        <translation>Annuler</translation>
    </message>
    <message>
        <location line="-665"/>
        <source>The registry repair did not finish. Reinstall Equalizer APO or see DeviceSelector.log for details.</source>
        <translation>La réparation du registre n’a pas abouti. Réinstallez Equalizer APO ou consultez DeviceSelector.log pour plus de détails.</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>OK</source>
        <translation>OK</translation>
    </message>
    <message>
        <source>Undoing the change did not finish either, so this device may be left partly changed. Reboot before trying again, and see the log for details.</source>
        <translation type="vanished">L’annulation de la modification n’a pas non plus abouti. Certaines modifications du périphérique peuvent donc subsister. Redémarrez avant de réessayer et consultez le journal pour plus de détails.</translation>
    </message>
    <message>
        <source>The device was left as it was before.</source>
        <translation type="vanished">Le périphérique est dans son état précédent.</translation>
    </message>
    <message>
        <source>Details are in %1.</source>
        <translation type="vanished">Les détails se trouvent dans %1.</translation>
    </message>
    <message>
        <location line="+608"/>
        <source>Close</source>
        <translation>Fermer</translation>
    </message>
    <message>
        <location line="+177"/>
        <source>APO will be installed</source>
        <translation>APO va être installé</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>APO will be uninstalled</source>
        <translation>APO va être désinstallé</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>APO will be upgraded</source>
        <translation>APO va être mis à niveau</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>APO installation will be changed</source>
        <translation>L&apos;installation d&apos;APO va être changée</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Audio enhancements will be enabled</source>
        <translation>Les améliorations audio seront activées</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>APO is already installed</source>
        <translation>APO est déjà installé</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>APO can be installed</source>
        <translation>APO peut être installé</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Voicemeeter was uninstalled</source>
        <translation>Voicemeeter a été désinstallé</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Default device</source>
        <translation>Périphérique par défaut</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Disabled</source>
        <translation>Désactivé</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Unplugged</source>
        <translation>Débranché</translation>
    </message>
</context>
<context>
    <name>DeviceSelectorClass</name>
    <message>
        <location filename="../DeviceSelector.ui" line="+176"/>
        <source>Use in ASIO apps</source>
        <translation>Utiliser dans les applications ASIO</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Adds `&lt;device&gt; - &lt;endpoint&gt; (EQ APO XT)` to the ASIO driver list. An application that picks it opens this device in WASAPI exclusive mode with the EQ applied.</source>
        <translation>Ajoute `&lt;périphérique&gt; - &lt;point de terminaison&gt; (EQ APO XT)` à la liste des pilotes ASIO. Une application qui la choisit ouvre ce périphérique en mode exclusif WASAPI avec l’égaliseur appliqué.</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.cpp" line="-810"/>
        <source>Troubleshooting options (only use in case of problems)</source>
        <translation>Options de dépannage (à utiliser uniquement en cas de problèmes)</translation>
    </message>
    <message>
        <location filename="../DeviceSelector.ui" line="-131"/>
        <source>Please select exactly one activated device by clicking on its name</source>
        <translation>Veuillez sélectionner exactement un périphérique activé en cliquant sur son nom</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+30"/>
        <source>Install APO</source>
        <translation>Installer APO</translation>
    </message>
    <message>
        <location line="-23"/>
        <location line="+81"/>
        <source>Use original APO</source>
        <translation>Utiliser l&apos;APO d&apos;origine</translation>
    </message>
    <message>
        <location line="-68"/>
        <source>Pre-mix:</source>
        <translation>Pre-mix:</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Post-mix:</source>
        <translation>Post-mix:</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Install as LFX/GFX</source>
        <translation>Installer en tant que LFX/GFX</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Install as SFX/MFX</source>
        <translation>Installer en tant que SFX/MFX</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Install as SFX/EFX</source>
        <translation>Installer en tant que SFX/EFX</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Automatically adjust install mode if problems with current mode are detected</source>
        <translation>Ajuster automatiquement le mode d&apos;installation si des problèmes avec le mode actuel sont détectés</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Auto:</source>
        <translation>Auto:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Allows to change silent input buffers to audible output buffers, so that delayed audio can be played.

CAUTION: Some sound card drivers can produce unwanted sounds when this is enabled.</source>
        <translation>Permet de changer les tampons (buffers) d&apos;entrée silencieux en tampons (buffers) de sortie audibles, afin que l&apos;audio retardé puisse être lu.

ATTENTION: Certains pilotes de carte son peuvent produire des sons indésirables lorsque cette option est activée.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Allow silent buffer modification</source>
        <translation>Autoriser la modification silencieuse du tampon</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Plays what Send: lines in other endpoints&apos; configurations hand to this endpoint, even when no program plays here. EqualizerAPOHost keeps a silent stream open on it from logon, and silent buffer modification is turned on with it.</source>
        <translation>Lit le son envoyé à ce point de terminaison par les lignes Send: des configurations des autres points de terminaison, même si aucun programme n’y lit de son. EqualizerAPOHost y maintient un flux silencieux ouvert dès l’ouverture de session. Cette option autorise aussi la modification des tampons silencieux.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Send</source>
        <translation>Envoi</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Remove the buffer</source>
        <translation>Supprimer le tampon</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Wait time:</source>
        <translation>Temps d&apos;attente :</translation>
    </message>
    <message>
        <location line="+105"/>
        <source>Tells the application that all channels use 64-bit float, so an application that mixes in that format can send its audio without converting it first. The device keeps its own format; the conversion happens after the EQ. If the application cannot open the device or the sound is wrong, turn this off.</source>
        <translation>Indique à l&apos;application que tous les canaux utilisent le format à virgule flottante 64 bits, ce qui permet à une application qui mixe dans ce format d&apos;envoyer l&apos;audio sans le convertir au préalable. Le périphérique conserve son propre format ; la conversion se fait après l&apos;EQ. Si l&apos;application ne peut pas ouvrir le périphérique ou si le son est incorrect, désactivez cette option.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Use 64-bit float samples</source>
        <translation>Échantillons flottants 64 bits</translation>
    </message>
    <message>
        <location line="-111"/>
        <location line="+10"/>
        <source>Sets how long to wait for processing once the buffer is removed. The longer the wait, the less sound without the EQ, but the more latency.</source>
        <translation>Définit combien de temps attendre le traitement une fois le tampon supprimé. Plus l&apos;attente est longue, moins il y a de son sans EQ, mais plus la latence augmente.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Up to a quarter of the buffer</source>
        <translation>Jusqu&apos;à un quart du tampon</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Up to half of the buffer</source>
        <translation>Jusqu&apos;à la moitié du tampon</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Up to three quarters of the buffer</source>
        <translation>Jusqu&apos;aux trois quarts du tampon</translation>
    </message>
    <message>
        <location line="-50"/>
        <source>Removing the buffer reduces latency. But when processing occasionally does not finish in time, sound without the EQ applied may come out for a moment. Applies to both input and output.</source>
        <translation>Supprimer le tampon réduit la latence. Mais si le traitement ne se termine parfois pas à temps, un son sans EQ peut être émis un instant. S&apos;applique à l&apos;entrée et à la sortie.</translation>
    </message>
    <message>
        <location line="+81"/>
        <source>Start the engine host automatically at boot</source>
        <translation>Démarrer automatiquement l&apos;hôte du moteur au démarrage du système</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Unchecked, the first application that opens the driver starts the host, and it leaves a minute after the last one closes it.</source>
        <translation>Désactivé, la première application qui ouvre le pilote lance l&apos;hôte, qui se ferme une minute après que la dernière l&apos;a refermé.</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>32-bit host support</source>
        <translation>Prise en charge des hôtes 32 bits</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Also registers the driver entry where 32-bit applications look for it. Unavailable when this build ships no 32-bit wrapper.</source>
        <translation>Enregistre aussi l&apos;entrée du pilote là où les applications 32 bits la cherchent. Indisponible quand cette version ne fournit pas de wrapper 32 bits.</translation>
    </message>
    <message>
        <source>Connector</source>
        <translation type="vanished">Connecteur</translation>
    </message>
    <message>
        <source>Device</source>
        <translation type="vanished">Périphérique</translation>
    </message>
    <message>
        <source>Status</source>
        <translation type="vanished">Statut</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>Please select the devices for which Equalizer APO is to be installed:</source>
        <translation>Veuillez sélectionner les appareils pour lesquels Equalizer APO doit être installé:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Copy Device command to clipboard</source>
        <translation>Copier la commande Device dans le presse-papiers</translation>
    </message>
</context>
<context>
    <name>DeviceTestDialog</name>
    <message>
        <source>Playback devices</source>
        <translation type="vanished">Périphériques de lecture</translation>
    </message>
    <message>
        <source>Capture devices</source>
        <translation type="vanished">Périphériques de capture</translation>
    </message>
    <message>
        <source>Error while accessing the registry</source>
        <translation type="vanished">Erreur durant l&apos;accès au registre</translation>
    </message>
    <message>
        <source>Cannot test APO installation as device is disabled</source>
        <translation type="vanished">Impossible de tester l&apos;installation d&apos;APO car le périphérique est désactivé</translation>
    </message>
    <message>
        <source>Cannot test APO installation as device is unplugged</source>
        <translation type="vanished">Impossible de tester l&apos;installation d&apos;APO car le périphérique est débranché</translation>
    </message>
    <message>
        <source>Error</source>
        <translation type="vanished">Erreur</translation>
    </message>
    <message>
        <source>Equalizer APO works but original APO could not be initialized. Maybe unset &quot;Use original APO&quot; in troubleshooting options.</source>
        <translation type="vanished">Equalizer APO fonctionne mais l&apos;APO d&apos;origine n&apos;a pas pu être initialisé. Essayez de désélectionner &quot;Utiliser l&apos;APO d&apos;origine&quot; dans les options de dépannage.</translation>
    </message>
</context>
<context>
    <name>DeviceTestDialogClass</name>
    <message>
        <source>Testing APO installation on devices...</source>
        <translation type="vanished">Test de l&apos;installation d&apos;APO sur les périphériques...</translation>
    </message>
    <message>
        <source>Connector</source>
        <translation type="vanished">Connecteur</translation>
    </message>
    <message>
        <source>Device</source>
        <translation type="vanished">Périphérique</translation>
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
        <translation>Impossible d’initialiser COM pour le test du périphérique.</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+2"/>
        <source>COM initialization failed (0x%1).</source>
        <translation>Échec de l’initialisation de COM (0x%1).</translation>
    </message>
    <message>
        <location line="+27"/>
        <location line="+189"/>
        <source>Restarting audio service...</source>
        <translation>Redémarrage du service audio...</translation>
    </message>
    <message>
        <location line="-184"/>
        <location line="+189"/>
        <source>Restart failed.</source>
        <translation>Echec du redémarrage.</translation>
    </message>
    <message>
        <location line="-174"/>
        <source>Checking APO installation...</source>
        <translation>Vérification de l&apos;installation d&apos;APO...</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Could not prepare the device test.</source>
        <translation>Impossible de préparer le test du périphérique.</translation>
    </message>
    <message>
        <location line="-38"/>
        <location line="+50"/>
        <source>Could not remove the device test registration: %1</source>
        <translation>Impossible de supprimer l’enregistrement du test du périphérique : %1</translation>
    </message>
    <message>
        <location line="+56"/>
        <source>Received unknown device GUID %1.</source>
        <translatorcomment>I don&apos;t have knowledge about GUID so I am not sure</translatorcomment>
        <translation>Réception d&apos;un GUID %1 de périphérique inconnu.</translation>
    </message>
    <message numerus="yes">
        <location line="+23"/>
        <location filename="../DeviceTestView.cpp" line="+517"/>
        <source>Check failed for %n device(s).</source>
        <translation>
            <numerusform>Echec de la vérification pour %n périphérique.</numerusform>
            <numerusform>Echec de la vérification pour %n périphériques.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Setting install mode for %1 %2 to %3.</source>
        <translation>Paramétrage du mode d&apos;installation pour %1 %2 à %3.</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Trying other configurations...</source>
        <translation>Tentative avec d&apos;autres configurations...</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Checks done. No problems were detected.</source>
        <translation>Vérifications effectuées. Aucun problème n&apos;a été détecté.</translation>
    </message>
    <message numerus="yes">
        <location line="+2"/>
        <location filename="../DeviceTestView.cpp" line="+1"/>
        <source>Checks done. Problems were detected for %n device(s).</source>
        <translation>
            <numerusform>Vérifications effectuées. Des problèmes ont été détectés pour %n périphérique.</numerusform>
            <numerusform>Vérifications effectuées. Des problèmes ont été détectés pour %n périphériques.</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>DeviceTestView</name>
    <message>
        <location filename="../DeviceTestDialog.cpp" line="+77"/>
        <source>Reinstalling %1 %2</source>
        <translation>Réinstallation de l&apos;APO sur %1 %2</translation>
    </message>
    <message>
        <location line="+80"/>
        <location line="+204"/>
        <location filename="../DeviceTestView.cpp" line="-47"/>
        <source>Checking</source>
        <translation>Vérification en cours</translation>
    </message>
    <message>
        <location line="-191"/>
        <location line="+413"/>
        <source>Details are in %1.</source>
        <translation>Les détails se trouvent dans %1.</translation>
    </message>
    <message>
        <location line="-245"/>
        <location line="+42"/>
        <location filename="../DeviceTestView.cpp" line="-1"/>
        <source>The APO works</source>
        <translation>L&apos;APO fonctionne</translation>
    </message>
    <message>
        <location line="-34"/>
        <source>Trying another install mode</source>
        <translation>Essai d&apos;un autre mode d&apos;installation</translation>
    </message>
    <message>
        <location line="+5"/>
        <location filename="../DeviceTestView.cpp" line="+12"/>
        <source>Pre-mix works. Checking post-mix</source>
        <translation>Pre-mix fonctionne. Vérification de Post-mix</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Post-mix works. Checking pre-mix</source>
        <translation>Post-mix fonctionne. Vérification de Pre-mix</translation>
    </message>
    <message>
        <location line="+71"/>
        <location line="+165"/>
        <location filename="../DeviceTestView.cpp" line="+58"/>
        <source>Windows audio</source>
        <translation>Audio Windows</translation>
    </message>
    <message>
        <location line="-47"/>
        <location line="+86"/>
        <location filename="../DeviceTestView.cpp" line="-8"/>
        <source>Not checked</source>
        <translation>Non vérifié</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Install undone</source>
        <translation>Installation annulée</translation>
    </message>
    <message>
        <location filename="../DeviceTestView.cpp" line="-444"/>
        <source>APO installation check</source>
        <translation>Vérification de l&apos;installation de l&apos;APO</translation>
    </message>
    <message>
        <location line="+53"/>
        <source>Record</source>
        <translation>Journal</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Stop</source>
        <translation>Arrêter</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Reinstall</source>
        <translation>Réinstaller</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Close</source>
        <translation>Fermer</translation>
    </message>
    <message>
        <location line="+79"/>
        <location line="+4"/>
        <source>The APO does not work</source>
        <translation>L&apos;APO ne fonctionne pas</translation>
    </message>
    <message>
        <source>All three install modes were tried and Windows did not load the APO on this device in any of them.</source>
        <translation type="vanished">Les trois modes d&apos;installation ont été essayés. Windows n&apos;a chargé l&apos;APO sur ce périphérique dans aucun d&apos;eux.</translation>
    </message>
    <message>
        <source>The cause could not be determined. Update the device&apos;s driver and check again; some drivers load no APO at all.</source>
        <translation type="vanished">La cause n&apos;a pas pu être déterminée. Mettez à jour le pilote du périphérique et relancez la vérification. Certains pilotes ne chargent aucun APO.</translation>
    </message>
    <message>
        <source>Windows did not load the APO on this device in the selected install mode (%1).</source>
        <translation type="vanished">Windows n&apos;a pas chargé l&apos;APO sur ce périphérique dans le mode d&apos;installation sélectionné (%1).</translation>
    </message>
    <message>
        <source>Turn on Automatic in the troubleshooting options, and the check tries the other modes too.</source>
        <translation type="vanished">Activez Auto dans les options de dépannage pour que la vérification essaie aussi les autres modes.</translation>
    </message>
    <message>
        <source>Pre-mix works, post-mix does not</source>
        <translation type="vanished">Pre-mix fonctionne, Post-mix ne fonctionne pas</translation>
    </message>
    <message>
        <source>The pre-mix APO works. Windows did not load the post-mix APO on this device.</source>
        <translation type="vanished">L&apos;APO Pre-mix fonctionne. Windows n&apos;a pas chargé l&apos;APO Post-mix sur ce périphérique.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Filters of the post-mix stage are not applied. Turn off post-mix install for this device, or try another install mode.</source>
        <translation>Les filtres de l&apos;étape Post-mix ne sont pas appliqués. Désactivez l&apos;installation Post-mix pour ce périphérique ou essayez un autre mode d&apos;installation.</translation>
    </message>
    <message>
        <source>Post-mix works, pre-mix does not</source>
        <translation type="vanished">Post-mix fonctionne, Pre-mix ne fonctionne pas</translation>
    </message>
    <message>
        <source>The post-mix APO works. Windows did not load the pre-mix APO on this device.</source>
        <translation type="vanished">L&apos;APO Post-mix fonctionne. Windows n&apos;a pas chargé l&apos;APO Pre-mix sur ce périphérique.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Filters of the pre-mix stage are not applied. Try another install mode.</source>
        <translation>Les filtres de l&apos;étape Pre-mix ne sont pas appliqués. Essayez un autre mode d&apos;installation.</translation>
    </message>
    <message>
        <source>The APO works, but the original APO could not start</source>
        <translation type="vanished">L&apos;APO fonctionne, mais l&apos;APO d&apos;origine n&apos;a pas pu démarrer</translation>
    </message>
    <message>
        <source>The equalizer works. The APO of the device&apos;s maker, which runs beside it, did not start, so that maker&apos;s effects are missing.</source>
        <translation type="vanished">L&apos;égaliseur fonctionne. L&apos;APO du fabricant du périphérique, qui s&apos;exécute en parallèle, n&apos;a pas démarré. Les effets du fabricant sont donc absents.</translation>
    </message>
    <message>
        <source>If you do not use those effects, turn off Use original APO in the troubleshooting options.</source>
        <translation type="vanished">Si vous n&apos;utilisez pas ces effets, désactivez &quot;Utiliser l&apos;APO d&apos;origine&quot; dans les options de dépannage.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The install mode could not be changed</source>
        <translation>Impossible de changer le mode d&apos;installation</translation>
    </message>
    <message>
        <source>The check tried to install in another mode and Windows refused the change. The device was left as it was before.</source>
        <translation type="vanished">La vérification a tenté une installation dans un autre mode, mais Windows a refusé la modification. Le périphérique est resté dans son état précédent.</translation>
    </message>
    <message>
        <source>Close programs that manage audio devices, then check again.</source>
        <translation type="vanished">Fermez les programmes qui gèrent les périphériques audio, puis relancez la vérification.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Not changed</source>
        <translation>Non modifié</translation>
    </message>
    <message>
        <source>Windows refused the change to this device. The device was left as it was before.</source>
        <translation type="vanished">Windows a refusé la modification de ce périphérique. Le périphérique est resté dans son état précédent.</translation>
    </message>
    <message>
        <source>Close programs that manage audio devices, then try again.</source>
        <translation type="vanished">Fermez les programmes qui gèrent les périphériques audio, puis réessayez.</translation>
    </message>
    <message>
        <source>Partly changed</source>
        <translation type="vanished">Modifié en partie</translation>
    </message>
    <message>
        <source>Windows refused the change, and undoing it did not finish either. The device may be left partly changed.</source>
        <translation type="vanished">Windows a refusé la modification et son annulation n&apos;a pas non plus abouti. Certaines modifications du périphérique peuvent donc subsister.</translation>
    </message>
    <message>
        <source>Restart the computer before trying again.</source>
        <translation type="vanished">Redémarrez l&apos;ordinateur avant de réessayer.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Not checked: the device is disabled</source>
        <translation>Non vérifié : périphérique désactivé</translation>
    </message>
    <message>
        <source>The device is disabled in Windows, so nothing could be played through it.</source>
        <translation type="vanished">Le périphérique est désactivé dans Windows. Aucun son n&apos;a donc pu être lu sur celui-ci.</translation>
    </message>
    <message>
        <source>Enable the device, then open the Device Selector and press OK to check again.</source>
        <translation type="vanished">Activez le périphérique, puis ouvrez Device Selector et cliquez sur OK pour relancer la vérification.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Not checked: the device is unplugged</source>
        <translation>Non vérifié : périphérique débranché</translation>
    </message>
    <message>
        <source>Nothing is plugged into this device, so nothing could be played through it.</source>
        <translation type="vanished">Rien n&apos;est branché sur ce périphérique. Aucun son n&apos;a donc pu être lu sur celui-ci.</translation>
    </message>
    <message>
        <source>Plug it in, then open the Device Selector and press OK to check again.</source>
        <translation type="vanished">Branchez le périphérique, puis ouvrez Device Selector et cliquez sur OK pour relancer la vérification.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Not checked: the device could not be opened</source>
        <translation>Non vérifié : impossible d&apos;ouvrir le périphérique</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing could be played through this device, so the APO on it could not answer.</source>
        <translation>Aucun son n&apos;a pu être lu sur ce périphérique. Son APO n&apos;a donc pas pu répondre.</translation>
    </message>
    <message>
        <source>Open the Device Selector and press OK to check again.</source>
        <translation type="vanished">Ouvrez Device Selector et cliquez sur OK pour relancer la vérification.</translation>
    </message>
    <message>
        <source>Windows audio could not be restarted, so the check did not run. The APO is installed, but it starts working only after Windows audio restarts.</source>
        <translation type="vanished">L&apos;audio Windows n&apos;a pas pu redémarrer. La vérification n&apos;a donc pas eu lieu. L&apos;APO est installé, mais il ne fonctionnera qu&apos;après le redémarrage de l&apos;audio Windows.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Restart the computer.</source>
        <translation>Redémarrez l&apos;ordinateur.</translation>
    </message>
    <message>
        <source>A Windows component the check needs did not start, so the check did not run.</source>
        <translation type="vanished">Un composant Windows nécessaire à la vérification n&apos;a pas démarré. La vérification n&apos;a donc pas eu lieu.</translation>
    </message>
    <message>
        <source>Restart the computer, then open the Device Selector and press OK.</source>
        <translation type="vanished">Redémarrez l&apos;ordinateur, puis ouvrez Device Selector et cliquez sur OK.</translation>
    </message>
    <message>
        <source>The cause could not be determined. Update the device&apos;s driver, then press Reinstall. Some drivers load no APO at all.</source>
        <translation type="vanished">La cause n&apos;a pas pu être déterminée. Mettez à jour le pilote du périphérique, puis cliquez sur &quot;Réinstaller&quot;. Certains pilotes ne chargent aucun APO.</translation>
    </message>
    <message>
        <source>Close programs that manage audio devices, then press Reinstall.</source>
        <translation type="vanished">Fermez les programmes qui gèrent les périphériques audio, puis cliquez sur &quot;Réinstaller&quot;.</translation>
    </message>
    <message>
        <source>Enable the device, then press Reinstall.</source>
        <translation type="vanished">Activez le périphérique, puis cliquez sur &quot;Réinstaller&quot;.</translation>
    </message>
    <message>
        <source>Plug it in, then press Reinstall.</source>
        <translation type="vanished">Branchez le périphérique, puis cliquez sur &quot;Réinstaller&quot;.</translation>
    </message>
    <message>
        <source>Press Reinstall to try again.</source>
        <translation type="vanished">Cliquez sur &quot;Réinstaller&quot; pour réessayer.</translation>
    </message>
    <message>
        <source>Windows blocked the setting the check uses to hear from the APO, so the check did not run.</source>
        <translation type="vanished">Windows a bloqué le réglage nécessaire à la réception des réponses de l&apos;APO. La vérification n&apos;a donc pas eu lieu.</translation>
    </message>
    <message>
        <source>Close security programs that block changes to system settings, then open the Device Selector and press OK.</source>
        <translation type="vanished">Fermez les programmes de sécurité qui bloquent les modifications des paramètres système, puis ouvrez Device Selector et cliquez sur OK.</translation>
    </message>
    <message>
        <source>Close security programs that block changes to system settings, then press Reinstall.</source>
        <translation type="vanished">Fermez les programmes de sécurité qui bloquent les modifications des paramètres système, puis cliquez sur &quot;Réinstaller&quot;.</translation>
    </message>
    <message>
        <source>The check lost its connection to the APO before every device had answered.</source>
        <translation type="vanished">La connexion avec l&apos;APO a été perdue avant que tous les périphériques aient répondu.</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>The list of audio devices could not be read, so no device was checked.</source>
        <translation>La liste des périphériques audio n&apos;a pas pu être lue. Aucun périphérique n&apos;a donc été vérifié.</translation>
    </message>
    <message>
        <source>The check was stopped. The APO is installed, but whether it works was not checked.</source>
        <translation type="vanished">La vérification a été arrêtée. L&apos;APO est installé, mais son fonctionnement n&apos;a pas été vérifié.</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>Checking that the APO works on each device (%1 of %2)</source>
        <translation>Vérification du fonctionnement de l&apos;APO sur chaque périphérique (%1 sur %2)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Restarting Windows audio</source>
        <translation>Redémarrage de l&apos;audio Windows</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Windows audio restarts during the check, so sound stops for a few seconds.</source>
        <translation>L&apos;audio Windows redémarre pendant la vérification. Le son est donc interrompu pendant quelques secondes.</translation>
    </message>
    <message numerus="yes">
        <location line="+6"/>
        <source>The APO does not work on %n device(s).</source>
        <translation>
            <numerusform>L&apos;APO ne fonctionne pas sur %n périphérique.</numerusform>
            <numerusform>L&apos;APO ne fonctionne pas sur %n périphériques.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The other devices are not affected.</source>
        <translation>Les autres périphériques ne sont pas affectés.</translation>
    </message>
    <message numerus="yes">
        <location line="+5"/>
        <source>%n device(s) could not be changed.</source>
        <translation>
            <numerusform>%n périphérique n&apos;a pas pu être modifié.</numerusform>
            <numerusform>%n périphériques n&apos;ont pas pu être modifiés.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The APO works on the devices that were checked.</source>
        <translation>L&apos;APO fonctionne sur les périphériques vérifiés.</translation>
    </message>
    <message numerus="yes">
        <source>The APO works. %n device(s) have something to look at.</source>
        <translation type="vanished">
            <numerusform>L&apos;APO fonctionne. %n périphérique présente un point à examiner.</numerusform>
            <numerusform>L&apos;APO fonctionne. %n périphériques présentent un point à examiner.</numerusform>
        </translation>
    </message>
    <message>
        <location line="-173"/>
        <source>All three install modes were tried and the APO was not installed.</source>
        <translation>Les trois modes d’installation ont été essayés, mais l’APO n’a pas été installé.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The cause is not known. Update the device&apos;s driver, then reinstall. Some drivers load no APO at all.</source>
        <translation>La cause est inconnue. Mettez à jour le pilote du périphérique, puis réinstallez. Certains pilotes ne chargent aucun APO.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The APO was not installed in the selected install mode (%1).</source>
        <translation>L’APO n’a pas été installé dans le mode d’installation sélectionné (%1).</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Select another install mode. The automatic install mode can be tried as well.</source>
        <translation>Sélectionnez un autre mode d’installation. Vous pouvez aussi essayer le mode d’installation automatique.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Post-mix does not work. Pre-mix works</source>
        <translation>Post-mix ne fonctionne pas. Pre-mix fonctionne</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The post-mix APO does not work. The pre-mix APO works.</source>
        <translation>L’APO Post-mix ne fonctionne pas. L’APO Pre-mix fonctionne.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Pre-mix does not work. Post-mix works</source>
        <translation>Pre-mix ne fonctionne pas. Post-mix fonctionne</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The pre-mix APO does not work. The post-mix APO works.</source>
        <translation>L’APO Pre-mix ne fonctionne pas. L’APO Post-mix fonctionne.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The original APO did not start. The APO works</source>
        <translation>L’APO d’origine n’a pas démarré. L’APO fonctionne</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The APO of the device&apos;s maker did not start, so the device&apos;s own sound effects are missing. The EQ APO works.</source>
        <translation>L’APO du fabricant n’a pas démarré. Les effets sonores propres au périphérique sont donc absents. EQ APO fonctionne.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The install in another mode was refused by Windows. The install was stopped.</source>
        <translation>Windows a refusé l’installation dans un autre mode. L’installation a été arrêtée.</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+4"/>
        <source>Close programs that manage audio devices, then reinstall.</source>
        <translation>Fermez les programmes qui gèrent les périphériques audio, puis réinstallez.</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Windows refused the change to this device. The install was stopped.</source>
        <translation>Windows a refusé la modification de ce périphérique. L’installation a été arrêtée.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Undo failed</source>
        <translation>Échec de l’annulation</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Windows refused the change, and undoing it failed as well.</source>
        <translation>Windows a refusé la modification et son annulation a aussi échoué.</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+20"/>
        <source>Restart the computer, then reinstall.</source>
        <translation>Redémarrez l’ordinateur, puis réinstallez.</translation>
    </message>
    <message>
        <location line="-17"/>
        <source>This device is disabled in Windows, so the APO cannot be installed on it.</source>
        <translation>Ce périphérique est désactivé dans Windows. L’APO ne peut donc pas y être installé.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enable the device.</source>
        <translation>Activez le périphérique.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Nothing is plugged into this device, so the APO cannot be installed on it.</source>
        <translation>Rien n’est branché sur ce périphérique. L’APO ne peut donc pas y être installé.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Plug the device in.</source>
        <translation>Branchez le périphérique.</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+16"/>
        <location line="+4"/>
        <source>Reinstall.</source>
        <translation>Réinstallez.</translation>
    </message>
    <message>
        <location line="-17"/>
        <source>Windows audio could not be restarted. The EQ APO works only after Windows audio restarts.</source>
        <translation>L’audio Windows n’a pas pu redémarrer. EQ APO ne fonctionne qu’après le redémarrage de l’audio Windows.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>A Windows error kept the installation check from starting.</source>
        <translation>Une erreur Windows a empêché le démarrage de la vérification de l’installation.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The EQ APO install was refused.</source>
        <translation>L’installation d’EQ APO a été refusée.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close security programs that block changes to system settings, then reinstall.</source>
        <translation>Fermez les programmes de sécurité qui bloquent les modifications des paramètres système, puis réinstallez.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The connection to the APO was lost.</source>
        <translation>La connexion avec l’APO a été perdue.</translation>
    </message>
    <message numerus="yes">
        <location line="+121"/>
        <source>%n device(s) have something to look at. The APO works.</source>
        <translation>
            <numerusform>%n périphérique présente un point à examiner. L’APO fonctionne.</numerusform>
            <numerusform>%n périphériques présentent un point à examiner. L’APO fonctionne.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The APO is not installed on any device.</source>
        <translation>L&apos;APO n&apos;est installé sur aucun périphérique.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The APO works on every device that could be checked.</source>
        <translation>L&apos;APO fonctionne sur tous les périphériques qui ont pu être vérifiés.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No device could be checked.</source>
        <translation>Aucun périphérique n&apos;a pu être vérifié.</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>%n device(s) could not be checked.</source>
        <translation>
            <numerusform>%n périphérique n&apos;a pas pu être vérifié.</numerusform>
            <numerusform>%n périphériques n&apos;ont pas pu être vérifiés.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The APO works on every device.</source>
        <translation>L&apos;APO fonctionne sur tous les périphériques.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The equalizer is applied from now on.</source>
        <translation>L&apos;égaliseur est désormais appliqué.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The install was stopped.</source>
        <translation>L’installation a été arrêtée.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The check did not finish.</source>
        <translation>La vérification n&apos;a pas abouti.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Whether the APO works is not known.</source>
        <translation>Le fonctionnement de l&apos;APO n&apos;a pas pu être confirmé.</translation>
    </message>
</context>
<context>
    <name>TitleBar</name>
    <message>
        <location filename="../../Editor/widgets/TitleBar.cpp" line="+119"/>
        <source>Restore</source>
        <translation>Restaurer</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Maximize</source>
        <translation>Agrandir</translation>
    </message>
</context>
</TS>
