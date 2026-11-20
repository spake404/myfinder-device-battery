using System;
using System.Collections.Generic;
using AtkBattery;

namespace AtkBatteryTray {
    internal static class TrayAutoSetup {
        const int PrimaryAssigned = 16;
        static readonly string[] Kinds = { "mouse", "keyboard", "headset", "gamepad" };
        static readonly string[] Names = { "鼠标", "键盘", "耳机", "手柄" };

        internal static bool NeedsDiscovery(AudioBindings preferences) {
            return preferences.AutoCreateTrayGroups && preferences.AutoTrayDiscovery!=TrayConfiguration.AutoDiscoveredMask;
        }

        internal static bool Discover(AudioBindings preferences, List<WirelessCard> candidates, DateTime now) {
            if(!NeedsDiscovery(preferences))return false;
            int original=preferences.AutoTrayDiscovery;
            var confirmed=candidates.FindAll(card=>Confirmed(card,now));
            if((original&PrimaryAssigned)==0) {
                var available=confirmed.FindAll(card=>TrayConfiguration.Owner(preferences,card.Id)==TrayConfiguration.FirstId);
                var first=TraySelection.Select(available,preferences.TraySource,preferences.TrayDeviceId,null,false,null);
                if(first!=null)preferences.AutoTrayDiscovery|=PrimaryAssigned|KindBit(first.Kind);
            }
            for(int index=0;index<Kinds.Length;index++) {
                int bit=1<<index;
                if((preferences.AutoTrayDiscovery&bit)!=0)continue;
                var matches=confirmed.FindAll(card=>card.Kind==Kinds[index]);
                if(matches.Count==0)continue;
                preferences.AutoTrayDiscovery|=bit;
                if(preferences.TrayGroups.Count>=3||preferences.TraySource==Kinds[index]||matches.Exists(card=>card.Id==preferences.TrayDeviceId||
                    TrayConfiguration.Owner(preferences,card.Id)!=TrayConfiguration.FirstId))continue;
                var group=TrayConfiguration.Add(preferences);
                group.Name=Names[index];
                foreach(var card in matches) {
                    if(group.Members.Count==64)break;
                    TrayConfiguration.Move(preferences,card.Id,group.Id);
                }
            }
            return original!=preferences.AutoTrayDiscovery;
        }

        static int KindBit(string kind) {
            int index=Array.IndexOf(Kinds,kind);
            return index<0?0:1<<index;
        }

        static bool Confirmed(WirelessCard card, DateTime now) {
            if(card.Hidden||card.AudioOnly||!TrayConfiguration.ValidId(card.Id)||KindBit(card.Kind)==0||
                card.Battery==null||!card.Battery.Percent.HasValue||card.Battery.Unconfirmed)return false;
            if(card.Device!=null&&(!card.Device.Online||card.Device.ConnectionState!="connected"))return false;
            return card.Battery.Fresh||card.Battery.SystemProvided&&UiState.Recent(card.Battery.QueriedAt,now,90);
        }
    }
}
