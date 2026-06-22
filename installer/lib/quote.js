/**
 * Quote a string for safe inclusion in a hook/statusline shell command.
 * Wraps in double quotes and escapes embedded double quotes and backslashes, so
 * paths with spaces (e.g. "/Users/My Name/...") survive. Avoids command
 * injection via crafted paths.
 */
export function quote(s) {
  const str = String(s);
  if (/^[A-Za-z0-9_\-./:=]+$/.test(str)) return str; // safe bare token
  return '"' + str.replace(/(["\\$`])/g, "\\$1") + '"';
}
