# Steam account and friends

The bundled `steam_context` add-on shows your Steam name, Steam ID and friends'
current presence automatically. It uses the Steam session already initialized by
Workshop Tools; no API key, account login form or manual profile setup is needed.

## Open the panel

In the project picker, click **Workshop Add-ons**, then choose
**Workshop Add-ons > Steam account and friends** in the add-ons window.
In Asset Browser, use the Workshop Add-ons menu; in Hammer, look under Help.

The account and friends list refresh about once per second. Select a friend and
click **Open selected Steam profile**, or use the buttons for your own profile
and the Steam friends overlay. These shortcuts require an available Steam overlay;
the picker may provide account information while its overlay remains unavailable.
An accepted request does not guarantee that Steam displays an overlay window.

Friends are displayed in pages of 16. The current cache is limited to the first
256 immediate friends; the panel reports the full count and any truncation.
Names are UTF-8 strings bounded to 127 bytes. Friend requests and blocked users
are excluded. No messages or invitations are sent by this example.

## SDK

Include `hammer_steam.h`, declare `HA_CAP_STEAM` (16384), and obtain the API with
`HA_GetSteam(host)` during `on_load`. Older hosts return null. Existing ABI-1
add-ons remain compatible because the extension pointer was appended.

```cpp
const HA_SteamV1* steam = HA_GetSteam(host);
HA_SteamStateV1 state{};
state.size = sizeof(state);
if (steam && steam->snapshot(host->context, &state) &&
    state.status == HA_STEAM_READY) {
    for (uint32_t i = 0; i < state.friend_count; ++i) {
        HA_SteamFriendV1 person{};
        person.size = sizeof(person);
        if (!steam->friend_at(host->context, state.revision, i, &person)) break;
        // Copy or display person.persona_name and person.steam_id.
    }
}
```

`snapshot` returns copied account state; `friend_at` reads the matching list
revision. Initialize each output's `size`; a failed call leaves its output
untouched. Check return values: calls can fail when the runtime is busy, the
add-on is inactive, the output is undersized or the list revision has changed.
A successful snapshot can still report `UNAVAILABLE` or `OFFLINE`.

`subscribe` delivers the initial state on the next GUI tick, followed by changed
snapshots only. Callbacks run on the editor GUI thread and receive borrowed data;
copy it to retain it. Keep callbacks short. Each add-on may own four subscriptions;
`unsubscribe` only removes its owner's handle. Worker threads may read the cache,
but do not call Steam directly through this API.

`open_profile` accepts the current account or a cached immediate friend.
`open_friends` requests the friends overlay. Both require a direct command or
panel-button callback on the GUI thread. Startup callbacks, automatic updates,
menu hooks and background jobs cannot open it. The provider rechecks the account,
AppID and overlay availability before issuing a request.

The complete [steam_context example](../addons/steam_context/steam_context.cpp)
demonstrates subscriptions, pagination, presence and both overlay actions. Generate
a standalone add-on with:

```powershell
python scripts/new-addon.py my_steam_panel --template steam_context --output ../my_steam_panel
```

## Availability and scope

The production provider operates in launcher-owned project-picker and tools
sessions. It borrows documented flat C exports from an already loaded,
compatibility-profiled `steam_api64.dll` and checks for the initialized CS2 AppID
730 session. It never initializes or shuts down Steam, pumps Steam callbacks,
loads a replacement Steam DLL or changes normal gameplay launch settings.

If Steam is offline, initialization is incomplete or its DLL build is unsupported,
the panel explains that state and clears cached account/friend data. An unavailable
Steam integration does not prevent other add-ons from working. Updates may require
a new compatibility profile; the provider does not guess interface versions.

This API exposes no trading, inventory, purchases, wallet, web cookies,
authentication tickets or arbitrary Steam interface pointers. Native add-on DLLs
are **not sandboxed**: limiting this API cannot prevent a malicious DLL from calling
other process or operating-system functions. Install only trusted add-ons.
The example keeps account/friend information in memory and does not write it to
settings or the loader log. Add-on authors should disclose any storage or sharing
of that information.

A Steam ID is a public identifier, **not proof of identity or ownership**. Do not
unlock paid features merely because a client supplies an ID. Authenticated services
need independently verified authentication and entitlement checks; see Valve's
[authentication and ownership documentation](https://partner.steamgames.com/doc/features/auth).

Networking, lobbies, session invitations and authentication are not exposed in
this version. Sharing CS2's Steam session requires explicit callback and connection
ownership before those operations can be supported without disrupting the host.

Provider declarations follow Valve's
[Steam API lifecycle](https://partner.steamgames.com/doc/api/steam_api) and
[ISteamFriends reference](https://partner.steamgames.com/doc/api/ISteamFriends).
