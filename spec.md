# ACL2 — visual spec

## Tokens

* **Identifiers**: `name`, `init_path`, `eth0`
  `[A-Za-z_][A-Za-z0-9_]*`
* **Types**: `int`, `float`, `bool`, `string`, `ref`
* **Literals**: integers (`123`), floats (`1.5`), bools (`true`/`false`), strings (`"text"`)
* **Symbols**: `{ } [ ] ( ) ; = , + - * / % ! < > & | ^ $ . :`
* **Comments**: `// single-line` and `/* block */`

Semicolons are required for field statements.

---

## Top-level blocks

Named blocks group fields and sub-blocks:

```
System {
    string name = "Atlas";
    int boot_delay = 200;
}
```

Block name is an identifier. Blocks may be repeated (e.g. multiple `interface` blocks).

---

## Fields

Two forms:

* **Typed field**:

  ```
  type name = expr;
  ```

  Example: `string init_path = "/sbin/init";`

* **Inferred type**:

  ```
  name = expr;
  ```

  Example: `debug = true;`

Arrays: `type[] name = { expr, expr, ... };`
Maps (optional): `{ "key": expr, ... }` (useful later).

---

## Sub-blocks (repeatable / named)

Blocks inside blocks; can be labeled with a string:

```
Network {
    interface "eth0" {
        bool dhcp = true;
        string gateway = "192.168.1.1";
    }

    interface "wlan0" {
        bool dhcp = false;
        string ip = "192.168.1.100";
    }
}
```

`interface "eth0"` is a repeated/named entry; the label is the quoted string.

---

## References

* **Global**: `$Path.to.field`
  Example: `$Network.interface["eth0"].gateway`
* **Local**: `$.field` (same block)
* **Parent**: `^field` (parent block)

Use `["name"]` index form to look up repeated/named blocks.

---

## Casting & expressions

* Cast: `(type)expr` — e.g. `(int)(1.5 * 256)`
* Usual C-like operators and precedence:

  * Unary: `! - + (type)expr`
  * Mul/Div/Mod: `* / %`
  * Add/Sub: `+ -` (also string concatenation for `+`)
  * Comparison: `< > <= >=`
  * Equality: `== !=`
  * Logical: `&& ||`
  * Ternary: `cond ? a : b` (optional)

Examples:

```
int max_procs = (int)(1.5 * 256);
string welcome = "Hi " + name;
bool ok = ($System.debug && (port > 1024));
```

---

## Minimal syntax rules (summary)

* Blocks: `Name { ... }`
* Field: `[type] ident = expr;`
* Array: `type[] ident = { e, e, ... };`
* Sub-block label: `ident "label" { ... }`
* `$` for global refs; `$.` local; `^` parent.
* Semicolons required; braces required for blocks.
* Strings double-quoted with C-like escapes.

---

## Compact example (what this looks like in practice)

```c
System {
    string name = "Atlas";
    int boot_delay = 200;
    bool debug = true;
}

Network {
    interface "eth0" {
        bool dhcp = true;
        string gateway = "192.168.1.1";
    }

    interface "wlan0" {
        bool dhcp = false;
        string ip = "192.168.1.100";
        string gateway = $Network.interface["eth0"].gateway;
    }
}

Services {
    service "getty" {
        string exec = "/sbin/getty";
        int ttys = $System.boot_delay > 0 ? 3 : 1;
    }
}

Modules {
    string[] load = { "virtio", "e1000" };
}
```