import requests

# Krok 1: Pobierz listę lotnisk
url_nool = "https://app.vacdm.net/api/vdgs/nool"

try:
    response = requests.get(url_nool)
    response.raise_for_status()
    data = response.json()
    airports = data.get("airports", {})
except Exception as e:
    print(f"Błąd podczas pobierania listy lotnisk: {e}")
    exit(1)

# Krok 2: Sprawdź każdy endpoint
for icao, endpoint_list in airports.items():
    if not endpoint_list:
        continue
    endpoint = endpoint_list[0]
    try:
        r = requests.get(endpoint)
        r.raise_for_status()
        flights = r.json().get("flights", [])
        if flights:
            print(f"{icao}: {len(flights)} aktywnych lotów.")
        else:
            print(f"{icao}: brak aktywnych lotów.")
    except Exception as e:
        print(f"{icao}: błąd przy zapytaniu ({e})")
