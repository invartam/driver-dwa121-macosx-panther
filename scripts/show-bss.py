#!/usr/bin/env python3
"""Print beacon observations from dwa121-info plist files; no association claim.

SSID bytes are escaped using JSON; binary SSIDs also retain their exact hex.
The registry snapshot contains nearby SSIDs and BSSIDs. No passwords are used.
"""
import argparse
import json
import plistlib
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probes', nargs='+', type=Path)
    args = parser.parse_args()
    for path in args.probes:
        p = plistlib.loads(path.read_bytes())
        result = {
            'probe': str(path),
            'received_channel': p['RXChannel'],
            'beacons': p['RXBeacons'],
            'bss_count': p['BSSCount'],
            'rejected': p['BSSRejected'],
            'table_drops': p['BSSDropped'],
            'networks': [],
        }
        for b in p['DiscoveredBSS']:
            result['networks'].append({
                'ssid': b['SSID'].decode('utf-8', errors='backslashreplace'),
                'ssid_hex': b['SSID'].hex(),
                'hidden': b['HiddenSSID'],
                'bssid': ':'.join(f'{v:02x}' for v in b['BSSID']),
                'advertised_channel': b['AdvertisedChannel'] or None,
                'received_channel': b['ReceivedChannel'],
                'observations': b['Observations'],
                'rsn_status': {0: 'absent', 1: 'valid', 2: 'malformed', 3: 'unsupported'}.get(b['RSNStatus'], 'unknown'),
                'wpa2_psk_ccmp_advertised': b['WPA2PSKCCMPAdvertised'],
                'pmf_required': b['PMFRequired'],
                'pmf_capable': b['PMFCapable'],
            })
        print(json.dumps(result, ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()
