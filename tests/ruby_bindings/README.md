# Ruby regression checks

Build on Windows with `ruby build_ruby.rb`; on Unix use extconf.rb and make
in an isolated source copy. Run `ruby -Ibin/ruby tests/ruby_bindings/run.rb`.
Optional arguments: CLI executable and a corpus root containing 94top50 and
psptop50 manifests. Both safe-early and exact modes are tested on this branch.

Ruby 3.4 APIs replace removed string/array internals. Every fight restores
input-order P-space, clears/private-resets it, then shares equal PINs (including
zero). P-space persists only within a match. `fixed_position` matches CLI -F.
Assembly and malformed warrior inputs raise exceptions. Tests are AI-assisted.
