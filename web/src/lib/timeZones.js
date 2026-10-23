import { useEffect, useState } from "react";

import { useConfig } from "./api";

// The firmware's zones (time_zone.c), the first its default. /status carries no clock,
// so the device's time is the browser's clock read in the zone's IANA name.
// "Europe/Kiev" is the older name, which every browser's zone data still resolves.
export const TIME_ZONES = [
  { key: "kyiv", iana: "Europe/Kiev", en: "Kyiv", uk: "Київ" },
  {
    key: "helsinki",
    iana: "Europe/Helsinki",
    en: "Chișinău, Bucharest, Helsinki, Athens",
    uk: "Кишинів, Бухарест, Гельсінкі, Афіни",
  },
  {
    key: "warsaw",
    iana: "Europe/Warsaw",
    en: "Warsaw, Berlin, Paris, Rome",
    uk: "Варшава, Берлін, Париж, Рим",
  },
  { key: "london", iana: "Europe/London", en: "London, Lisbon", uk: "Лондон, Лісабон" },
  { key: "istanbul", iana: "Europe/Istanbul", en: "Istanbul", uk: "Стамбул" },
  { key: "utc", iana: "UTC", en: "UTC", uk: "UTC" },
  { key: "dubai", iana: "Asia/Dubai", en: "Dubai", uk: "Дубай" },
  { key: "tokyo", iana: "Asia/Tokyo", en: "Tokyo", uk: "Токіо" },
  { key: "sydney", iana: "Australia/Sydney", en: "Sydney", uk: "Сідней" },
  { key: "new_york", iana: "America/New_York", en: "New York, Toronto", uk: "Нью-Йорк, Торонто" },
  { key: "chicago", iana: "America/Chicago", en: "Chicago", uk: "Чикаго" },
  { key: "denver", iana: "America/Denver", en: "Denver", uk: "Денвер" },
  { key: "los_angeles", iana: "America/Los_Angeles", en: "Los Angeles", uk: "Лос-Анджелес" },
];

// The device's zone; Kyiv on a firmware whose /config has none.
export function useDeviceZone() {
  const { data: config } = useConfig();
  return TIME_ZONES.find((zone) => zone.key === config?.time_zone) ?? TIME_ZONES[0];
}

// The time now, kept current while the page is open.
export function useNow() {
  const [now, setNow] = useState(() => new Date());

  useEffect(() => {
    const timer = setInterval(() => setNow(new Date()), 10000);
    return () => clearInterval(timer);
  }, []);

  return now;
}
