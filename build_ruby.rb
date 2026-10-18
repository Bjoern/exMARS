# Windows RubyInstaller + GCC build, isolated from CLI/Python outputs.
# ruby build_ruby.rb [OUTPUT_DIR]
require 'rbconfig'
require 'fileutils'
require 'json'
require 'digest'
require 'shellwords'
abort 'This helper targets Windows RubyInstaller; use extconf.rb on other platforms.' unless RUBY_PLATFORM.include?('mingw')
root = __dir__
out = File.expand_path(ARGV[0] || 'bin/ruby', root)
FileUtils.mkdir_p(out)
config = RbConfig::CONFIG
def native(path)
  path.sub(%r{\A/([A-Za-z])/}, '\1:/')
end
sources = %w[rubyinterface.c pmars.c pspace.c sim.c].map { |f| File.join(root, f) }
cmd = Shellwords.split(ENV.fetch('CC', config['CC'])) + %w[-shared -O3 -std=gnu17 -DRUBYINTERFACE]
cmd += ['-I' + native(config['rubyhdrdir']), '-I' + native(config['rubyarchhdrdir'])]
cmd += sources + ['-L' + native(config['libdir'])] + Shellwords.split(config['LIBRUBYARG_SHARED'])
cmd += ['-o', File.join(out, 'Mars.' + config['DLEXT'])]
puts cmd.join(' ')
abort 'Ruby extension build failed' unless system(*cmd)
FileUtils.cp(File.join(root, 'Warrior.rb'), out)
inputs = sources + Dir[File.join(root, '*.h'), File.join(root, '*.inc')]
manifest = {ruby: RUBY_DESCRIPTION, command: cmd, sources: inputs.to_h { |f| [File.basename(f), Digest::SHA256.file(f).hexdigest] }, binary_sha256: Digest::SHA256.file(File.join(out, 'Mars.' + config['DLEXT'])).hexdigest}
File.write(File.join(out, 'build.json'), JSON.pretty_generate(manifest) + "\n")
puts "Built #{out}"
