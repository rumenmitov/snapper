<start name="vfs" ram="24M">
 	<provides><service name="File_system"/></provides>
 	<config>
 		<vfs>
 			<dir name="dev">
 				<block name="block" block_buffer_count="128"/>
 			</dir>
			<lwext4 block_device="/dev/block" cache_write_back="yes"
				      expand_via_io="yes" writeable="yes"
				      reporting="no"
				      external_cache_size="0M" report_cache="no"/>
 		</vfs>
 		<policy label_prefix="snapper" root="/" writeable="yes"/>
 	</config>
</start>

<start name="snapper" ram="400M">
	<config       verbose="true"
                redundancy="2"
                integrity="true"
                max_snapshots="0"
                min_snapshots="0"
                threshold="100"
                expiration="3600">
    <rtc/>
		<vfs>
			<fs/>
		</vfs>
	</config>
  <route>
		<service name="Rtc"> <any-child/> </service>
		<any-service> <parent/> <any-child/> </any-service>
	</route>
</start>

<!-- Local Variables: -->
<!-- mode: xml -->
<!-- End: -->
