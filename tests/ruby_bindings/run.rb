# ruby -Ibuild/ruby tests/ruby_bindings/run.rb [CLI] [HILLS_ROOT]
require 'Mars'
require 'tempfile'
require 'open3'
require 'json'
FIXTURES = File.expand_path('../pmars_compat', __dir__)
$checks = 0
$failures = 0
def check(name, got, expected)
  $checks += 1
  if got == expected
    puts "ok #{name}"
  else
    $failures += 1
    puts "FAIL #{name}: #{got.inspect} != #{expected.inspect}"
  end
end
def rejected(name)
  begin
    yield
    check(name, 'accepted', 'rejected')
  rescue ArgumentError, RangeError, TypeError
    check(name, 'rejected', 'rejected')
  end
end
def rows(m, *ws)
  m.fight(*ws).results_str.lines.map(&:strip)
end
def parse_text(m, text)
  Tempfile.create(['ruby-warrior', '.red'], __dir__) do |f|
    f.write(";redcode-94\n;assert 1\n" + text + "\nend\n")
    f.flush
    m.parse(f.path)
  end
end
def marker(n)
  "ldp.ab #0,r\nsne.ab #7999,r\nstp.ab ##{n},#1\nldp.ab #1,r\nseq.ab ##{n},r\ndat 0,0\njmp 0\nr dat 0,0"
end
[true, false].each do |early|
  m = Mars.standard(2)
  check('default early exit', m.early_exit, true)
  m.early_exit = early
  m.fixed_position = 4000
  first = parse_text(m, "ldp.ab #0,r\nseq.ab #7999,r\ndat 0,0\njmp 0\nr dat 0,0")
  idle = m.parse(File.join(FIXTURES, 'idle.red'))
  2.times { |i| check("fresh P-space #{early}/#{i}", rows(m, first, idle), ['0 1', '0 1']) }
  a, b = [11, 22].map { |n| parse_text(m, marker(n)) }
  [2, 3, 2, 4, 1].each do |n|
    m.rounds = n
    check("ownership #{early}/#{n}", rows(m, a, b), ["0 #{n}"] * 2)
  end
  m.rounds = 10
  writer, reader = %w[pin_writer.red pin_reader.red].map { |f| m.parse(File.join(FIXTURES, f)) }
  check('parsed PIN', [writer.pin, reader.pin], [123, 123])
  check("PIN sharing #{early}", rows(m, writer, reader), ['0 10'] * 2)
  writer.pin = nil
  check("remove PIN #{early}", rows(m, writer, reader), ['10 0', '0 0'])
  writer.pin = reader.pin = 0
  check("PIN zero #{early}", rows(m, writer, reader), ['0 10'] * 2)
  reader.pin = 1
  check("different PIN #{early}", rows(m, writer, reader), ['10 0', '0 0'])
  check('wins accessor', m.wins(0, 1), 10)
  check('loss accessor', m.losses(1), 10)
  check('scores', m.scores, [30, 0])
  %w[expression immediate_b immediate_b_stp wide_fields seed_zero].each do |name|
    core = name == 'wide_fields' ? 55440 : 8000
    m = Mars.new(2, core, 1000, 8000, 100, 100)
    m.early_exit = early
    m.rounds = name == 'seed_zero' ? 20 : 10
    m.fixed_position = name == 'seed_zero' ? 100 : 4000
    files = name == 'seed_zero' ? %w[idle.red seed_zero.red] : [name + '.red', 'idle.red']
    ws = files.map { |f| m.parse(File.join(FIXTURES, f)) }
    check("engine #{name}/#{early}", rows(m, *ws), name == 'seed_zero' ? ['0 19', '1 19'] : ['0 10'] * 2)
  end
end
m = Mars.standard(2)
rejected('bad position') { m.fixed_position = 99 }
rejected('position above CLI limit') { m.fixed_position = 1073741825 }
rejected('bad rounds') { m.rounds = 0 }
rejected('negative cycles') { Mars.new(2, 8000, -1, 8000, 100, 100) }
rejected('empty warrior') { m.fight(Warrior.new, Warrior.new) }
rejected('wrong count') { m.fight(Warrior.new) }
rejected('assembly failure') { parse_text(m, 'not_an_opcode 1,2') }
rejected('oversized warrior') do
  w = Warrior.new
  w.instructions = Array.new(101) { Instruction.new('DAT', 'F', '$', 0, '$', 0) }
  m.fight(w, w)
end
[ Mars.tiny(2), Mars.new(2, 80, 800, 80, 5, 5), Mars.standard(3) ].each do |engine|
  w = parse_text(engine, 'jmp 0')
  engine.rounds = 2
  engine.fight(*Array.new(engine.warriors, w))
  check("custom settings #{engine.coresize}/#{engine.warriors}", engine.wins(0, engine.warriors), 2)
end
nano = Mars.new(2, 80, 800, 80, 5, 5)
check('permutations (nano)', nano.permutations, 142)
rejected('permutate with three warriors') { Mars.standard(3).permutate = true }
# icws88 is pMARS -8: later parses enforce ICWS'88 rules. Off by default.
w88 = "start mov bomb, <ptr\njmp start\nptr dat #0, #10\nbomb dat #0, #0"
m88 = Mars.standard(2)
check('icws88 defaults to false', m88.icws88, false)
check('without icws88, > is accepted', parse_text(m88, w88.sub('<ptr', '>ptr')).class, Warrior)
m88.icws88 = true
check('icws88: an 88 warrior is accepted', parse_text(m88, w88).class, Warrior)
['mov bomb, >ptr', 'mov bomb, }ptr', 'mov.i bomb, <ptr', 'seq bomb, <ptr', 'mov bomb, #ptr', 'jmp #0'].each do |bad|
  rejected("icws88: #{bad} is rejected") { parse_text(m88, w88.sub('mov bomb, <ptr', bad)) }
end
rejected('icws88: ORG is rejected') { parse_text(m88, "org start\n" + w88) }
m88.icws88 = false
check('icws88 can be switched off again', parse_text(m88, w88.sub('<ptr', '>ptr')).class, Warrior)
sources = ["add.ab #13, 2\nmov.i 2, @1\njmp -2\ndat 0, 0", "spl 0\nmov.i -1, 7\njmp -1"]
Tempfile.create(['perm-a', '.red'], __dir__) do |fa|
  Tempfile.create(['perm-b', '.red'], __dir__) do |fb|
    [fa, fb].zip(sources).each { |f, s| f.write(";redcode-94\n;assert 1\n#{s}\nend\n"); f.flush }
    ws = [fa, fb].map { |f| nano.parse(f.path) }
    nano.permutate, nano.rounds, nano.fixed_position = true, nano.permutations, 17
    got = rows(nano, *ws).map { |l| l.split.take(2).join(' ') }
    check('permutate plays every pair once', got.sum { |l| l.split[0].to_i } + got[0].split[1].to_i, 142)
    if ARGV[0]
      out, err, status = Open3.capture3(ARGV[0], '-s', '80', '-c', '800', '-p', '80', '-l', '5', '-d', '5',
                                        '-P', '-F', '17', fa.path, fb.path)
      raise err unless status.success?
      check('permutate: Ruby = CLI -P', got, out.lines.last(2).map { |l| l.split.take(2).join(' ') })
    end
  end
end
if ARGV[1]
  %w[94top50 psptop50].each do |suite|
    dir = File.join(ARGV[1], suite)
    files = JSON.parse(File.read(File.join(dir, 'suite.benchmark.json'), encoding: 'bom|utf-8'))['warriors']
    m = Mars.standard(2)
    parsed = files.map { |f| m.parse(File.join(dir, f)) }
    check("parse all #{suite}", parsed.length, files.length)
    [true, false].each do |early|
      12.times do |i|
        indices = [i, (i + 17) % files.length]
        m.early_exit, m.rounds, m.fixed_position = early, 200, 4000 + i
        got = rows(m, *indices.map { |j| parsed[j] })
        fresh = Mars.standard(2)
        fresh.early_exit, fresh.rounds, fresh.fixed_position = early, 200, 4000 + i
        check("#{suite}/#{early}/#{i} reused=fresh", got, rows(fresh, *indices.map { |j| parsed[j] }))
        out, err, status = Open3.capture3(ARGV[0], *(early ? [] : ['-X']), '-r', '200', '-F', (4000 + i).to_s, *indices.map { |j| File.join(dir, files[j]) })
        raise err unless status.success?
        check("#{suite}/#{early}/#{i} Ruby=CLI", got, out.lines.last(2).map { |l| l.split.take(2).join(' ') })
      end
    end
  end
end
GC.start
puts "#{$checks} checks; #{$failures} failures"
exit($failures.zero? ? 0 : 1)
