require 'rake'
require 'rake/clean'

NAME = 'smartdd'

CC = ENV['CC'] || 'clang'
CFLAGS = "#{ENV['CFLAGS']} -Wall -pedantic"
LDFLAGS = ENV['LDFLAGS']

SOURCES = FileList['main.c']
OBJECTS = SOURCES.ext('o')
CLEAN.include(OBJECTS).include(NAME)

task :default => NAME

file NAME => OBJECTS do 
	sh "#{CC} #{LDFLAGS} #{OBJECTS} -o #{NAME}" 
end

rule '.o' => '.c' do |file| 
	sh "#{CC} #{CFLAGS} -c #{file.source} -o #{file}" 
end

